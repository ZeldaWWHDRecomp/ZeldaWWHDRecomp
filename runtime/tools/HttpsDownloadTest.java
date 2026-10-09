import org.wwhdrecomp.wwhd.HttpsDownload;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.charset.StandardCharsets;

public final class HttpsDownloadTest {
    public static void main(String[] args) throws Exception {
        Path directory = Files.createTempDirectory("wwhd-https-test-");
        Path file = directory.resolve("download");
        try {
            if (HttpsDownload.download("http://example.com", file.toString(), 100).isEmpty() || Files.exists(file))
                throw new AssertionError("HTTP was accepted");
            if (args.length != 0) {
                String error = HttpsDownload.download(args[0], file.toString(), 2 * 1024 * 1024);
                if (!error.isEmpty() || Files.size(file) == 0) throw new AssertionError(error);
                byte[] previous = Files.readAllBytes(file);
                if (HttpsDownload.download(args[0], file.toString(), 100).isEmpty()
                        || !java.util.Arrays.equals(previous, Files.readAllBytes(file)))
                    throw new AssertionError("Existing file changed");
                System.out.println("Downloaded " + previous.length + " bytes over HTTPS");
                Files.delete(file);
                if (HttpsDownload.download(args[0], file.toString(), 1).isEmpty() || Files.exists(file))
                    throw new AssertionError("Oversized partial download retained");
            }
        } finally {Files.deleteIfExists(file); Files.delete(directory);}
    }
}
