// Minimal Faust architecture: set params from argv, render N samples, dump raw floats.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <cmath>
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"

struct ParamUI : public UI {
    std::map<std::string, FAUSTFLOAT*> zones;
    void addButton(const char* l, FAUSTFLOAT* z) override { zones[l]=z; }
    void addCheckButton(const char* l, FAUSTFLOAT* z) override { zones[l]=z; }
    void addVerticalSlider(const char* l, FAUSTFLOAT* z, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT) override { zones[l]=z; }
    void addHorizontalSlider(const char* l, FAUSTFLOAT* z, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT) override { zones[l]=z; }
    void addNumEntry(const char* l, FAUSTFLOAT* z, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT) override { zones[l]=z; }
    void addHorizontalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) override {}
    void addVerticalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) override {}
    void addSoundfile(const char*, const char*, Soundfile**) override {}
    void openTabBox(const char*) override {} void openHorizontalBox(const char*) override {}
    void openVerticalBox(const char*) override {} void closeBox() override {}
    void declare(FAUSTFLOAT*, const char*, const char*) override {}
};
struct NullMeta : public Meta { void declare(const char*, const char*) override {} };

<<includeIntrinsic>>
<<includeclass>>

int main(int argc, char** argv) {
    int SR = 48000, N = 48000;
    mydsp dsp; NullMeta m; dsp.metadata(&m);
    dsp.init(SR);
    ParamUI ui; dsp.buildUserInterface(&ui);
    // params: name=value pairs; "@sample:name=value" schedules a change
    std::vector<std::pair<int,std::pair<std::string,float>>> sched;
    for (int i=1;i<argc;i++) {
        std::string a=argv[i]; int at=0;
        if (a[0]=='@') { size_t c=a.find(':'); at=atoi(a.substr(1,c-1).c_str()); a=a.substr(c+1); }
        size_t e=a.find('=');
        if (e==std::string::npos) continue;
        sched.push_back({at,{a.substr(0,e), (float)atof(a.substr(e+1).c_str())}});
    }
    int nin=dsp.getNumInputs(), nout=dsp.getNumOutputs();
    const int B=64;
    std::vector<std::vector<float>> in(nin, std::vector<float>(B)), out(nout, std::vector<float>(B));
    std::vector<float*> ip(nin), op(nout);
    for(int c=0;c<nin;c++) ip[c]=in[c].data();
    for(int c=0;c<nout;c++) op[c]=out[c].data();
    long t=0;
    for (long blk=0; blk<N/B; blk++) {
        for (auto& s : sched) if (s.first>=t && s.first<t+B) { auto it=ui.zones.find(s.second.first); if(it!=ui.zones.end()) *it->second = s.second.second; }
        for(int c=0;c<nin;c++) for(int i=0;i<B;i++){ long n=t+i; in[c][i] = (c==0)? 0.3f*sinf(2.f*3.14159265f*110.f*n/SR) : 0.5f; }
        dsp.compute(B, ip.data(), op.data());
        for(int i=0;i<B;i++) for(int c=0;c<nout;c++) fwrite(&out[c][i], 4, 1, stdout);
        t+=B;
    }
    return 0;
}
