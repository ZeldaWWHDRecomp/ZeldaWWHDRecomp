// Small bounded JSON reader/writer for manifests and player profiles.
#pragma once
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <tuple>
#include <utility>
namespace mods::json {
struct Value {
    enum Type { Null, Bool, Number, String, Array, Object } type=Null;
    bool boolean=false; double number=0; std::string text;
    std::vector<Value> array; std::map<std::string,Value> object;
    Value()=default;
    Value(bool b):type(Bool),boolean(b){}
    Value(double n):type(Number),number(n){}
    Value(int n):Value(double(n)){}
    Value(std::string s):type(String),text(std::move(s)){}
    Value(const char* s):Value(std::string(s)){}
    Value& operator[](const std::string& key) { type=Object;return object[key]; }
    const Value& get(const std::string& key) const {
        static const Value empty;auto it=object.find(key);return it==object.end()?empty:it->second;
    }
    std::string string(const std::string& fallback={}) const { return type==String?text:fallback; }
    bool operator==(const Value& v) const {
        return type==v.type && boolean==v.boolean && number==v.number && text==v.text && array==v.array && object==v.object;
    }
};
inline std::string quote(const std::string& s) {
    std::string out="\"";const char* hex="0123456789abcdef";
    for(unsigned char c:s) {
        if(c=='"'||c=='\\') { out+='\\';out+=char(c); }
        else if(c<32) { out+="\\u00";out+=hex[c>>4];out+=hex[c&15]; }
        else out+=char(c);
    }
    return out+'"';
}
inline std::string dump(const Value& v) {
    switch(v.type) {
    case Value::Null:return "null";
    case Value::Bool:return v.boolean?"true":"false";
    case Value::Number:{ if(!std::isfinite(v.number))throw std::runtime_error("Nonfinite JSON number");std::ostringstream s;s<<std::setprecision(17)<<v.number;return s.str(); }
    case Value::String:return quote(v.text);
    case Value::Array:{ std::string s="[";for(const auto& a:v.array){if(s.size()>1)s+=',';s+=dump(a);}return s+']'; }
    case Value::Object:{std::string s="{";for(const auto& [k,a]:v.object){if(s.size()>1)s+=',';s+=quote(k)+':'+dump(a);}return s+'}';}
    }
    return "null";
}
class Reader {
    const std::string& s;size_t p=0;
    [[noreturn]] void fail(const char* message) const {throw std::runtime_error(std::string(message)+" at byte "+std::to_string(p));}
    void ws(){while(p<s.size() && (s[p]==' '||s[p]=='\n'||s[p]=='\r'||s[p]=='\t'))++p;}
    bool take(char c){ws();if(p<s.size()&&s[p]==c){++p;return true;}return false;}
    unsigned hex4(){unsigned n=0;for(int i=0;i<4;i++){if(p>=s.size())fail("Short Unicode escape");char c=s[p++];int d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;if(d<0)fail("Invalid Unicode escape");n=n*16+d;}return n;}
    void utf8(std::string& out,unsigned n){if(n<128)out+=char(n);else if(n<2048){out+=char(192|(n>>6));out+=char(128|(n&63));}else if(n<65536){out+=char(224|(n>>12));out+=char(128|((n>>6)&63));out+=char(128|(n&63));}else{out+=char(240|(n>>18));out+=char(128|((n>>12)&63));out+=char(128|((n>>6)&63));out+=char(128|(n&63));}}
    std::string str(){
        if(!take('"'))fail("Expected string");std::string out;
        while(p<s.size()) {
            unsigned char c=s[p++];if(c=='"')return out;if(c<32)fail("Control byte in string");
            if(c!='\\'){out+=char(c);continue;}
            if(p==s.size())fail("Short escape");c=s[p++];
            switch(c){case '"':case '\\':case '/':out+=char(c);break;case 'b':out+='\b';break;case 'f':out+='\f';break;case 'n':out+='\n';break;case 'r':out+='\r';break;case 't':out+='\t';break;case 'u':{unsigned n=hex4();if(n>=0xd800&&n<=0xdbff){if(p+2>s.size()||s[p++]!='\\'||s[p++]!='u')fail("Missing surrogate");unsigned low=hex4();if(low<0xdc00||low>0xdfff)fail("Invalid surrogate");n=0x10000+((n-0xd800)<<10)+low-0xdc00;}else if(n>=0xdc00&&n<=0xdfff)fail("Unexpected surrogate");utf8(out,n);break;}default:fail("Invalid escape");}
        }
        fail("Unterminated string");
    }
    Value value(unsigned depth){
        if(depth>32)fail("JSON nesting limit");ws();if(p==s.size())fail("Unexpected end");Value v;
        if(s[p]=='"')return Value(str());
        if(take('{')){v.type=Value::Object;if(take('}'))return v;do{auto key=str();if(!take(':'))fail("Expected colon");if(v.object.contains(key))fail("Duplicate key");v.object.emplace(key,value(depth+1));if(take('}'))return v;}while(take(','));fail("Expected object separator");}
        if(take('[')){v.type=Value::Array;if(take(']'))return v;do{if(v.array.size()>=65536)fail("Array limit");v.array.push_back(value(depth+1));if(take(']'))return v;}while(take(','));fail("Expected array separator");}
        for(auto [literal,type,b]:{std::tuple{"null",Value::Null,false},std::tuple{"true",Value::Bool,true},std::tuple{"false",Value::Bool,false}}){std::string l=literal;if(s.compare(p,l.size(),l)==0){p+=l.size();v.type=type;v.boolean=b;return v;}}
        size_t start=p;if(s[p]=='-')++p;
        if(p==s.size())fail("Short number");
        if(s[p]=='0')++p;else{if(s[p]<'1'||s[p]>'9')fail("Expected number");while(p<s.size()&&s[p]>='0'&&s[p]<='9')++p;}
        if(p<s.size()&&s[p]=='.'){++p;size_t begin=p;while(p<s.size()&&s[p]>='0'&&s[p]<='9')++p;if(p==begin)fail("Short fraction");}
        if(p<s.size()&&(s[p]=='e'||s[p]=='E')){++p;if(p<s.size()&&(s[p]=='+'||s[p]=='-'))++p;size_t begin=p;while(p<s.size()&&s[p]>='0'&&s[p]<='9')++p;if(p==begin)fail("Short exponent");}
        std::string n=s.substr(start,p-start);v.type=Value::Number;v.number=std::strtod(n.c_str(),nullptr);if(!std::isfinite(v.number))fail("Number overflow");return v;
    }
public:
    explicit Reader(const std::string& input):s(input){}
    Value read(){if(s.size()>1024*1024)fail("JSON size limit");auto v=value(0);ws();if(p!=s.size())fail("Trailing JSON input");return v;}
};
inline Value parse(const std::string& s){return Reader(s).read();}
} // namespace mods::json
