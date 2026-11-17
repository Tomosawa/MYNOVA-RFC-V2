#pragma once
#include <string>
#include <algorithm>
#include <cctype>
class String : public std::string {
public:
    using std::string::string;
    using std::string::operator=;
    String(const std::string& s) : std::string(s) {}
    void trim() {
        auto first=find_first_not_of(" \r\n\t");
        if (first==npos) clear();
        else *this=substr(first,find_last_not_of(" \r\n\t")-first+1);
    }
    int indexOf(char c) const {auto i=find(c);return i==npos ? -1 : (int)i;}
    String substring(size_t first, size_t last=npos) const {return substr(first,last==npos ? npos : last-first);}
};
template<class A,class B,class C> inline A constrain(A value,B lo,C hi) {
    return std::min(std::max(value,(A)lo),(A)hi);
}
