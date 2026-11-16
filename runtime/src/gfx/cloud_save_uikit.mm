// cloud_save.h for the UIKit host: the save (its user/*.sav files) in the private database's "Saves" zone. "Save"/
// "current" is the manifest: each file's name, size and content hash, the whole save's hash, when the game last wrote
// it and on which device. Each file's content without its trailing zeros is a "SaveContent" record named by its hash,
// stored once however many files have it; a file of zeros has none. (The Picto Box keeps every photo slot as a 320 KB
// file, mostly zeros: the 36 of them were 11 MB sent whole each time.) A sync is one atomic change: the new content,
// the manifest (refused if another device changed it meanwhile) and the deletion of content no longer listed.
// This device remembers the hash it last synced (cloud-save.json in the config folder): a side whose hash still
// matches it hasn't changed since, so the other side's save is the newer one.
// Before the zone, the save was one record in the default zone with the files packed in an asset ("legacy"): read
// when the zone has none, deleted once the zone has the save.
#import <CloudKit/CloudKit.h>
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#include <CommonCrypto/CommonDigest.h>
#include <sys/stat.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

#include "cloud_save.h"
#include "../platform/host.h"

void log_msg(const char* fmt, ...);

namespace fs = std::filesystem;

namespace cloud_save {
namespace {
constexpr const char* kMagic = "WWHDSAVE1\n";
const double kWait = 10.0;  // seconds for each CloudKit call at start-up

struct Files {
    std::map<std::string, std::string> data;  // user/<name> -> bytes
    std::string hash;                         // "" when there is no cking.sav
    double saved = 0;                         // cking.sav's modification time (seconds since 1970)
};

CKContainer* g_container = nil;
std::string g_dir;  // the save folder
std::mutex g_mu;    // one sync at a time
std::atomic<bool> g_running{false};
std::atomic<bool> g_upload_now{false};  // the uploader's first round right away (start-up leaves uploads to it)
std::atomic<bool> g_move{false};        // iCloud has the save's old one-record form: upload it to the zone, unchanged too

// "Checking your save in iCloud..." over the black screen before the game's first picture, when the check takes
// longer than a moment
UILabel* g_label = nil;
std::atomic<int> g_status{0};  // which check the label belongs to; 0: none

void show_status() {
    const int mine = ++g_status;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, int64_t(0.4 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        if (g_status != mine || g_label) return;
        UIView* view = nil;
        for (UIScene* scene in UIApplication.sharedApplication.connectedScenes)
            if ([scene isKindOfClass:UIWindowScene.class])
                for (UIWindow* w in ((UIWindowScene*)scene).windows)
                    if (w.isKeyWindow) view = w.rootViewController.view;
        if (!view) return;
        g_label = [UILabel new];
        g_label.text = @"Checking your save in iCloud\u2026";
        g_label.textColor = UIColor.whiteColor;
        g_label.font = [UIFont preferredFontForTextStyle:UIFontTextStyleHeadline];
        g_label.textAlignment = NSTextAlignmentCenter;
        g_label.frame = view.bounds;
        g_label.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
        [view addSubview:g_label];
    });
}

void hide_status() {
    g_status = 0;
    dispatch_async(dispatch_get_main_queue(), ^{
        [g_label removeFromSuperview];
        g_label = nil;
    });
}

std::string hex(const unsigned char* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; i++) s += d[p[i] >> 4], s += d[p[i] & 15];
    return s;
}

std::string sha256(const std::string& bytes) {
    unsigned char md[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(bytes.data(), (CC_LONG)bytes.size(), md);
    return hex(md, sizeof md);
}

// a file's content without its trailing zeros ("" for a file of zeros)
std::string trimmed(const std::string& bytes) {
    const size_t end = bytes.find_last_not_of('\0');
    return end == std::string::npos ? std::string() : bytes.substr(0, end + 1);
}

bool safe_name(const std::string& name) { return !name.empty() && name.find('/') == std::string::npos && name[0] != '.'; }

// the manifest: a line per file, "<name>\t<size>\t<content hash, empty for zeros>"
struct Entry {
    size_t size = 0;
    std::string hash;
};
using Manifest = std::map<std::string, Entry>;

std::string manifest_text(const Files& f) {
    std::string out;
    for (auto& [name, bytes] : f.data) {
        const std::string t = trimmed(bytes);
        out += name + "\t" + std::to_string(bytes.size()) + "\t" + (t.empty() ? "" : sha256(t)) + "\n";
    }
    return out;
}

bool parse_manifest(NSString* text, Manifest& out) {
    if (![text isKindOfClass:NSString.class]) return false;
    std::istringstream in(text.UTF8String);
    std::string line;
    while (std::getline(in, line)) {
        const size_t a = line.find('\t'), b = a == std::string::npos ? a : line.find('\t', a + 1);
        if (b == std::string::npos) return false;
        const std::string name = line.substr(0, a), hash = line.substr(b + 1);
        if (!safe_name(name) || (!hash.empty() && hash.size() != 64)) return false;
        out[name] = Entry{size_t(strtoull(line.substr(a + 1, b - a - 1).c_str(), nullptr, 10)), hash};
    }
    return out.count("cking.sav") == 1;
}

std::set<std::string> content_hashes(const Manifest& m) {
    std::set<std::string> out;
    for (auto& [name, e] : m)
        if (!e.hash.empty()) out.insert(e.hash);
    return out;
}

Files read_files(const std::string& dir) {
    Files f;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(fs::path(dir) / "user", ec)) {
        if (!e.is_regular_file(ec) || e.path().extension() != ".sav") continue;
        std::ifstream in(e.path(), std::ios::binary);
        f.data[e.path().filename().string()] = std::string((std::istreambuf_iterator<char>(in)), {});
    }
    if (!f.data.count("cking.sav")) return Files{};
    CC_SHA256_CTX c;
    CC_SHA256_Init(&c);
    for (auto& [name, bytes] : f.data) {
        const uint64_t sizes[2] = {name.size(), bytes.size()};
        CC_SHA256_Update(&c, sizes, sizeof sizes);
        CC_SHA256_Update(&c, name.data(), (CC_LONG)name.size());
        CC_SHA256_Update(&c, bytes.data(), (CC_LONG)bytes.size());
    }
    unsigned char md[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(md, &c);
    f.hash = hex(md, sizeof md);
    struct stat st;
    if (stat((fs::path(dir) / "user" / "cking.sav").c_str(), &st) == 0)
        f.saved = double(st.st_mtimespec.tv_sec) + st.st_mtimespec.tv_nsec / 1e9;
    return f;
}

std::string pack(const Files& f) {
    std::string out = kMagic;
    for (auto& [name, bytes] : f.data) out += name + "\n" + std::to_string(bytes.size()) + "\n" + bytes;
    return out;
}

bool unpack(const std::string& in, std::map<std::string, std::string>& out) {
    if (in.compare(0, strlen(kMagic), kMagic) != 0) return false;
    size_t p = strlen(kMagic);
    while (p < in.size()) {
        const size_t a = in.find('\n', p), b = a == std::string::npos ? a : in.find('\n', a + 1);
        if (b == std::string::npos) return false;
        const std::string name = in.substr(p, a - p);
        const size_t size = strtoull(in.substr(a + 1, b - a - 1).c_str(), nullptr, 10);
        if (!safe_name(name) || b + 1 + size > in.size()) return false;
        out[name] = in.substr(b + 1, size);
        p = b + 1 + size;
    }
    return out.count("cking.sav") == 1;
}

fs::path state_file() { return fs::path(host::config_dir()) / "cloud-save.json"; }

std::string synced_hash() {
    NSData* d = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:state_file().c_str()]];
    NSDictionary* j = d ? [NSJSONSerialization JSONObjectWithData:d options:0 error:nil] : nil;
    NSString* h = [j isKindOfClass:NSDictionary.class] ? j[@"synced"] : nil;
    return [h isKindOfClass:NSString.class] ? h.UTF8String : "";
}

void set_synced(const std::string& hash) {
    NSData* d = [NSJSONSerialization dataWithJSONObject:@{@"synced": [NSString stringWithUTF8String:hash.c_str()]} options:0 error:nil];
    [d writeToFile:[NSString stringWithUTF8String:state_file().c_str()] atomically:YES];
}

std::string local_time(double t) {
    if (t <= 0) return "unknown";
    NSDateFormatter* f = [NSDateFormatter new];
    f.dateStyle = NSDateFormatterMediumStyle;
    f.timeStyle = NSDateFormatterShortStyle;
    return [f stringFromDate:[NSDate dateWithTimeIntervalSince1970:t]].UTF8String;
}

// the save files of a side about to be replaced, into save-backups/icloud-<time>-<what>
void backup(const std::map<std::string, std::string>& files, const char* what) {
    if (files.empty()) return;
    char stamp[32];
    const time_t now = time(nullptr);
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&now));
    const fs::path dir = fs::path(g_dir).parent_path() / "save-backups" / (std::string("icloud-") + stamp + "-" + what) / "user";
    std::error_code ec;
    fs::create_directories(dir, ec);
    for (auto& [name, bytes] : files) std::ofstream(dir / name, std::ios::binary) << bytes;
    log_msg("[cloud save] the %s save is kept in %s", what, dir.parent_path().c_str());
}

// the downloaded save in place of this device's: each file written whole and renamed over, the others removed
bool apply(const std::map<std::string, std::string>& files) {
    const fs::path user = fs::path(g_dir) / "user";
    std::error_code ec;
    fs::create_directories(user, ec);
    for (auto& [name, bytes] : files) {
        const fs::path tmp = user / ("." + name + ".download");
        {
            std::ofstream out(tmp, std::ios::binary);
            out << bytes;
            if (!out.good()) return false;
        }
        fs::rename(tmp, user / name, ec);
        if (ec) return false;
    }
    for (const auto& e : fs::directory_iterator(user, ec))
        if (e.path().extension() == ".sav" && !files.count(e.path().filename().string())) fs::remove(e.path(), ec);
    return true;
}

// one CloudKit call, waited for (at most kWait seconds at start-up, longer later)
bool wait_for(void (^start)(dispatch_semaphore_t), double seconds) {
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    start(done);
    return dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, int64_t(seconds * NSEC_PER_SEC))) == 0;
}

CKRecordZoneID* zone_id() { return [[CKRecordZoneID alloc] initWithZoneName:@"Saves" ownerName:CKCurrentUserDefaultName]; }
CKRecordID* manifest_id() { return [[CKRecordID alloc] initWithRecordName:@"current" zoneID:zone_id()]; }
CKRecordID* content_id(const std::string& hash) {
    return [[CKRecordID alloc] initWithRecordName:[NSString stringWithUTF8String:hash.c_str()] zoneID:zone_id()];
}
CKRecordID* legacy_id() { return [[CKRecordID alloc] initWithRecordName:@"current"]; }

// the save in iCloud: the zone's manifest, or before the move the default zone's one record
struct Remote {
    CKRecord* record = nil;
    CKRecord* legacy = nil;
    std::string hash;
    double saved = 0;
    std::string device = "another device";
    bool any() const { return record || legacy; }
};

// one record by ID: OK with RECORD (nil: none there), or not OK (no network, signed out, ...)
bool fetch_record(CKRecordID* rid, CKRecord* __strong& record, double seconds) {
    __block CKRecord* got = nil;
    __block NSError* failed = nil;
    const bool answered = wait_for(^(dispatch_semaphore_t done) {
        [g_container.privateCloudDatabase fetchRecordWithID:rid completionHandler:^(CKRecord* r, NSError* e) {
            got = r, failed = e;
            dispatch_semaphore_signal(done);
        }];
    }, seconds);
    if (!answered) {
        log_msg("[cloud save] iCloud didn't answer in time");
        return false;
    }
    if (failed && failed.code != CKErrorUnknownItem && failed.code != CKErrorZoneNotFound) {
        log_msg("[cloud save] reading the save in iCloud failed: %s", failed.localizedDescription.UTF8String);
        return false;
    }
    record = got;
    return true;
}

bool fetch(Remote& out, double seconds) {
    out = Remote{};
    CKRecord* r = nil;
    if (!fetch_record(manifest_id(), r, seconds)) return false;
    if (!r && !fetch_record(legacy_id(), out.legacy, seconds)) return false;
    out.record = r;
    CKRecord* m = r ?: out.legacy;
    if (!m) return true;
    NSString* h = m[@"hash"];
    NSDate* when = m[@"saved"];
    NSString* d = m[@"device"];
    out.hash = [h isKindOfClass:NSString.class] ? h.UTF8String : "";
    out.saved = [when isKindOfClass:NSDate.class] ? when.timeIntervalSince1970 : 0;
    if ([d isKindOfClass:NSString.class]) out.device = d.UTF8String;
    return true;
}

// the content records of HASHES, each checked against its hash: hash -> trimmed content
bool fetch_contents(const std::set<std::string>& hashes, std::map<std::string, std::string>& out, double seconds) {
    if (hashes.empty()) return true;
    NSMutableArray<CKRecordID*>* ids = [NSMutableArray array];
    for (auto& h : hashes) [ids addObject:content_id(h)];
    __block NSMutableDictionary<NSString*, NSData*>* got = [NSMutableDictionary dictionary];
    __block NSError* failed = nil;
    CKFetchRecordsOperation* op = [[CKFetchRecordsOperation alloc] initWithRecordIDs:ids];
    op.qualityOfService = NSQualityOfServiceUserInitiated;
    const bool answered = wait_for(^(dispatch_semaphore_t done) {
        op.fetchRecordsCompletionBlock = ^(NSDictionary<CKRecordID*, CKRecord*>* records, NSError* e) {
            for (CKRecordID* rid in records) {
                CKAsset* asset = records[rid][@"data"];
                NSData* d = asset.fileURL ? [NSData dataWithContentsOfURL:asset.fileURL] : nil;
                if (d) got[rid.recordName] = d;
            }
            failed = e;
            dispatch_semaphore_signal(done);
        };
        [g_container.privateCloudDatabase addOperation:op];
    }, seconds);
    if (!answered || failed) {
        log_msg("[cloud save] reading the save's pictures in iCloud failed: %s",
                answered ? failed.localizedDescription.UTF8String : "no answer in time");
        return false;
    }
    for (auto& h : hashes) {
        NSData* d = got[[NSString stringWithUTF8String:h.c_str()]];
        std::string bytes = d ? std::string((const char*)d.bytes, d.length) : std::string();
        if (!d || sha256(bytes) != h) {
            log_msg("[cloud save] a part of the save in iCloud is missing or damaged");
            return false;
        }
        out[h] = std::move(bytes);
    }
    return true;
}

// the save in iCloud as files; content this device's LOCAL files already have isn't downloaded
bool download(const Remote& r, std::map<std::string, std::string>& files, const Files& local, double seconds) {
    if (!r.record) {
        CKAsset* asset = r.legacy[@"files"];
        NSData* d = asset.fileURL ? [NSData dataWithContentsOfURL:asset.fileURL] : nil;
        return d && unpack(std::string((const char*)d.bytes, d.length), files);
    }
    Manifest m;
    if (!parse_manifest(r.record[@"manifest"], m)) return false;
    std::map<std::string, std::string> have;
    for (auto& [name, bytes] : local.data)
        if (std::string t = trimmed(bytes); !t.empty()) have[sha256(t)] = std::move(t);
    std::set<std::string> missing;
    for (auto& h : content_hashes(m))
        if (!have.count(h)) missing.insert(h);
    if (!fetch_contents(missing, have, seconds)) return false;
    for (auto& [name, e] : m) {
        std::string bytes = e.hash.empty() ? std::string() : have[e.hash];
        if (bytes.size() > e.size) return false;
        bytes.resize(e.size, '\0');
        files[name] = std::move(bytes);
    }
    log_msg("[cloud save] downloaded %zu of the save's %zu parts", missing.size(), content_hashes(m).size());
    return true;
}

// the zone, made once per run (saving one that exists changes nothing)
std::atomic<bool> g_zone_ready{false};
bool make_zone(double seconds) {
    if (g_zone_ready) return true;
    __block NSError* failed = nil;
    CKModifyRecordZonesOperation* op = [[CKModifyRecordZonesOperation alloc]
        initWithRecordZonesToSave:@[[[CKRecordZone alloc] initWithZoneID:zone_id()]] recordZoneIDsToDelete:nil];
    op.qualityOfService = NSQualityOfServiceUserInitiated;
    const bool answered = wait_for(^(dispatch_semaphore_t done) {
        op.modifyRecordZonesCompletionBlock = ^(NSArray*, NSArray*, NSError* e) {
            failed = e;
            dispatch_semaphore_signal(done);
        };
        [g_container.privateCloudDatabase addOperation:op];
    }, seconds);
    if (!answered || failed) {
        log_msg("[cloud save] making the save's place in iCloud failed: %s",
                answered ? failed.localizedDescription.UTF8String : "no answer in time");
        return false;
    }
    return g_zone_ready = true;
}

// one record change, waited for: SAVE (refused if any changed on the server since it was read) and DELETE, all or none
bool modify(NSArray<CKRecord*>* save, NSArray<CKRecordID*>* remove, double seconds, NSError* __strong& error) {
    __block NSError* failed = nil;
    CKModifyRecordsOperation* op = [[CKModifyRecordsOperation alloc] initWithRecordsToSave:save recordIDsToDelete:remove];
    op.savePolicy = CKRecordSaveIfServerRecordUnchanged;
    op.atomic = YES;
    op.qualityOfService = NSQualityOfServiceUserInitiated;
    const bool answered = wait_for(^(dispatch_semaphore_t done) {
        op.modifyRecordsCompletionBlock = ^(NSArray*, NSArray*, NSError* e) {
            failed = e;
            dispatch_semaphore_signal(done);
        };
        [g_container.privateCloudDatabase addOperation:op];
    }, seconds);
    error = failed;
    return answered && !failed;
}

// this device's save as the zone's manifest, with the content it lists that iCloud hasn't got, and without what the
// manifest R replaces listed and this one doesn't (all refused if another device saved meanwhile)
bool upload(const Remote& r, const Files& f, double seconds) {
    if (!make_zone(seconds)) return false;
    Manifest old;
    if (r.record) parse_manifest(r.record[@"manifest"], old);
    const std::set<std::string> had = content_hashes(old);
    std::set<std::string> listed;
    NSMutableArray<CKRecord*>* save = [NSMutableArray array];
    NSMutableArray<NSString*>* temps = [NSMutableArray array];
    size_t bytes_up = 0;
    bool ok = true;
    for (auto& [name, bytes] : f.data) {
        const std::string t = trimmed(bytes);
        if (t.empty()) continue;
        const std::string h = sha256(t);
        if (!listed.insert(h).second || had.count(h)) continue;
        NSString* tmp = [NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
        [temps addObject:tmp];
        if (![[NSData dataWithBytes:t.data() length:t.size()] writeToFile:tmp atomically:YES]) {
            ok = false;
            break;
        }
        CKRecord* c = [[CKRecord alloc] initWithRecordType:@"SaveContent" recordID:content_id(h)];
        c[@"data"] = [[CKAsset alloc] initWithFileURL:[NSURL fileURLWithPath:tmp]];
        [save addObject:c];
        bytes_up += t.size();
    }
    NSMutableArray<CKRecordID*>* remove = [NSMutableArray array];
    for (auto& h : had)
        if (!listed.count(h)) [remove addObject:content_id(h)];
    CKRecord* m = r.record ?: [[CKRecord alloc] initWithRecordType:@"Save" recordID:manifest_id()];
    m[@"manifest"] = [NSString stringWithUTF8String:manifest_text(f).c_str()];
    m[@"hash"] = [NSString stringWithUTF8String:f.hash.c_str()];
    m[@"saved"] = [NSDate dateWithTimeIntervalSince1970:f.saved];
    const char* device = getenv("WWHD_MACHINE_NAME");
    m[@"device"] = [NSString stringWithUTF8String:device ? device : "?"];
    [save addObject:m];
    NSError* failed = nil;
    if (ok) ok = modify(save, remove, seconds, failed);
    for (NSString* tmp in temps) [[NSFileManager defaultManager] removeItemAtPath:tmp error:nil];
    if (!ok) {
        if (failed.code == CKErrorZoneNotFound) g_zone_ready = false;
        log_msg("[cloud save] uploading the save failed: %s", failed ? failed.localizedDescription.UTF8String : "no answer in time");
        return false;
    }
    set_synced(f.hash);
    log_msg("[cloud save] uploaded the save (saved %s): %lu new parts, %zu KB; %lu no longer used, removed",
            local_time(f.saved).c_str(), (unsigned long)(save.count - 1), bytes_up / 1024, (unsigned long)remove.count);
    if (r.legacy) {
        NSError* e = nil;
        if (modify(@[], @[legacy_id()], seconds, e)) log_msg("[cloud save] the save's old one-record form in iCloud removed");
    }
    return true;
}

// while the game runs: a save that changed since the last sync goes up, unless another device's is there now (then
// the next start asks which to keep)
// WAIT_WRITES: a save from the last 3 s (the game may still be writing it) is waited for rather than left for later
void upload_if_changed(double seconds, bool wait_writes = false) {
    std::lock_guard<std::mutex> guard(g_mu);
    Files f = read_files(g_dir);
    const std::string synced = synced_hash();
    if (f.hash.empty() || (f.hash == synced && !g_move)) return;
    if (const double age = [NSDate date].timeIntervalSince1970 - f.saved; f.saved > 0 && age < 3) {
        if (!wait_writes) return;
        std::this_thread::sleep_for(std::chrono::duration<double>(3 - age));
        f = read_files(g_dir);
        if (f.hash.empty() || f.hash == synced) return;
    }
    Remote r;
    if (!fetch(r, seconds)) return;
    if (f.hash == synced && !r.legacy) {  // (another device moved it)
        g_move = false;
        return;
    }
    if (r.any() && r.hash != synced && r.hash != f.hash) {
        static std::string told;
        if (told != r.hash)
            log_msg("[cloud save] another device saved to iCloud while this one played: this save stays here until the next start");
        told = r.hash;
        return;
    }
    if (upload(r, f, seconds)) g_move = false;
}
}  // namespace

void startup(const std::string& save_dir, const Ask& ask) {
    NSString* name = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"WWHDCloudKitContainer"];
    if (![name isKindOfClass:NSString.class] || !name.length) return;
    g_container = [CKContainer containerWithIdentifier:name];
    g_dir = save_dir;
    show_status();
    __block CKAccountStatus status = CKAccountStatusCouldNotDetermine;
    wait_for(^(dispatch_semaphore_t done) {
        [g_container accountStatusWithCompletionHandler:^(CKAccountStatus s, NSError*) {
            status = s;
            dispatch_semaphore_signal(done);
        }];
    }, kWait);
    if (status != CKAccountStatusAvailable) {
        hide_status();
        log_msg("[cloud save] no iCloud account here (status %ld): the save stays on this device", (long)status);
        return;
    }
    {
        std::lock_guard<std::mutex> guard(g_mu);
        const Files local = read_files(g_dir);
        const std::string synced = synced_hash();
        Remote r;
        if (!fetch(r, kWait)) {
            log_msg("[cloud save] playing this device's save; it goes to iCloud when iCloud answers");
        } else if (!r.any()) {
            g_upload_now = !local.hash.empty();
        } else {
            if (r.legacy) g_move = g_upload_now = true;
            const std::string& there = r.hash;
            const std::string& device = r.device;
            const double there_saved = r.saved;
            std::map<std::string, std::string> files;
            if (there == local.hash) {
                set_synced(there);
            } else if (local.hash.empty() || local.hash == synced) {
                if (download(r, files, local, kWait)) {
                    backup(local.data, "this-device");
                    if (apply(files)) {
                        set_synced(there);
                        log_msg("[cloud save] using the save from iCloud (saved %s on %s)", local_time(there_saved).c_str(),
                                device.c_str());
                    } else {
                        log_msg("[cloud save] writing the save from iCloud failed: this device's save is in save-backups");
                    }
                } else {
                    log_msg("[cloud save] the save in iCloud couldn't be read: keeping this device's");
                }
            } else if (there == synced) {
                g_upload_now = true;
            } else {
                // both changed since this device last synced
                const std::string text = "The save in iCloud and this device's save have both changed.\n\n"
                                         "In iCloud: saved " + local_time(there_saved) + " on " + device + "\n"
                                         "On this device: saved " + local_time(local.saved) + "\n\n"
                                         "Which save do you want to play? The other one is kept in save-backups.";
                hide_status();
                const int pick = ask ? ask("Wind Waker HD: iCloud save", text, {"Use the iCloud save", "Keep this device's save"}) : -1;
                if (pick == 0 && download(r, files, local, kWait)) {
                    backup(local.data, "this-device");
                    if (apply(files)) set_synced(there), log_msg("[cloud save] using the save from iCloud (chosen)");
                } else if (pick == 1) {
                    if (download(r, files, local, kWait)) backup(files, "icloud");
                    set_synced(there);  // (iCloud's save is the one this device replaces)
                    g_upload_now = true;
                } else {
                    log_msg("[cloud save] both saves changed and no choice was made: playing this device's, iCloud's stays");
                }
            }
        }
    }
    hide_status();
    if (!g_running.exchange(true))
        std::thread([] {
            for (;;) {
                if (!g_upload_now.exchange(false)) std::this_thread::sleep_for(std::chrono::seconds(20));
                @autoreleasepool { upload_if_changed(60); }
            }
        }).detach();
}

void flush() {
    if (!g_container) return;
    __block UIBackgroundTaskIdentifier task = UIBackgroundTaskInvalid;
    task = [UIApplication.sharedApplication beginBackgroundTaskWithExpirationHandler:^{
        [UIApplication.sharedApplication endBackgroundTask:task];
        task = UIBackgroundTaskInvalid;
    }];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        @autoreleasepool { upload_if_changed(20); }
        dispatch_async(dispatch_get_main_queue(), ^{
            if (task != UIBackgroundTaskInvalid) [UIApplication.sharedApplication endBackgroundTask:task];
            task = UIBackgroundTaskInvalid;
        });
    });
}

void flush_then(std::function<void()> done) {
    std::thread([done] {
        if (g_container) @autoreleasepool { upload_if_changed(20, true); }
        done();
    }).detach();
}
}  // namespace cloud_save
