#include <cstdio>
#include <cmath>
#include <cstring>
#include <string>
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
struct NUI : public UI {
  void addButton(const char*,FAUSTFLOAT*) override{} void addCheckButton(const char*,FAUSTFLOAT*) override{}
  void addVerticalSlider(const char*,FAUSTFLOAT*,FAUSTFLOAT,FAUSTFLOAT,FAUSTFLOAT,FAUSTFLOAT) override{}
  void addHorizontalSlider(const char*,FAUSTFLOAT*,FAUSTFLOAT,FAUSTFLOAT,FAUSTFLOAT,FAUSTFLOAT) override{}
  void addNumEntry(const char*,FAUSTFLOAT*,FAUSTFLOAT,FAUSTFLOAT,FAUSTFLOAT,FAUSTFLOAT) override{}
  void addHorizontalBargraph(const char*,FAUSTFLOAT*,FAUSTFLOAT,FAUSTFLOAT) override{}
  void addVerticalBargraph(const char*,FAUSTFLOAT*,FAUSTFLOAT,FAUSTFLOAT) override{}
  void addSoundfile(const char*,const char*,Soundfile**) override{}
  void openTabBox(const char*) override{} void openHorizontalBox(const char*) override{}
  void openVerticalBox(const char*) override{} void closeBox() override{}
};
struct NM : public Meta { void declare(const char*,const char*) override{} };
<<includeIntrinsic>>
<<includeclass>>
int main(){
  mydsp d; NM m; d.metadata(&m); d.init(48000); NUI u; d.buildUserInterface(&u);
  const int N=200001; static float in[N], o0[N], o1[N];
  for(int i=0;i<N;i++) in[i]=(float)i/(N-1);          // exact 0.0 .. exact 1.0
  float* ip[1]={in}; float* op[2]={o0,o1};
  d.compute(N, ip, op);
  printf("x=0.0  tabulated=%.9g  exact=%.9g  diff=%.3g\n", o0[0], o1[0], (double)(o0[0]-o1[0]));
  printf("x=1.0  tabulated=%.9g  exact=%.9g  diff=%.3g\n", o0[N-1], o1[N-1], (double)(o0[N-1]-o1[N-1]));
  double md=0; int mi=0;
  for(int i=0;i<N;i++){ double dd=fabs((double)o0[i]-o1[i]); if(dd>md){md=dd;mi=i;} }
  printf("max |diff| over [0,1] = %.4g at x=%.5f  (value there = %.6f)\n", md, (double)mi/(N-1), (double)o1[mi]);
  // worst relative pitch error: cf = f*(1 + p*K), K = 2^(range*7)
  for (double K : {1.0, 8.0, 128.0}) {
    double worst=0, at=0;
    for(int i=0;i<N;i++){
      double p=o1[i], dp=fabs((double)o0[i]-o1[i]);
      double rel=K*dp/(1.0+p*K);
      if(rel>worst){worst=rel; at=(double)i/(N-1);}
    }
    printf("  slide range K=%-5g -> worst frequency error %.3g relative = %.5f cents (at x=%.3f)\n",
           K, worst, 1200.0*log2(1.0+worst), at);
  }
  return 0;
}
