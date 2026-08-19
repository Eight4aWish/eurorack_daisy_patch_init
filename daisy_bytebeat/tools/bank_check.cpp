#include "bytebeat_engine.h"
#include "formulas.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

static void run(BytebeatEngine& e, int n, double& rms, double& mn, double& mx) {
    double acc=0; mn=1e9; mx=-1e9;
    for(int i=0;i<n;i++){ float a=0,b=0; e.Process(a,b); acc+=a*a; if(a<mn)mn=a; if(a>mx)mx=a; }
    rms=std::sqrt(acc/n);
}

int main(){
    printf("GetFormulaCount()   = %d\n", GetFormulaCount());
    printf("GetNumberedSlotCount= %d\n", GetNumberedSlotCount());
    printf("GetReferenceIndex() = %d\n\n", GetReferenceIndex());

    printf("Bank boundaries (my kBanks table assumes 0/20/40/60/80/100):\n");
    for (int i : {0,19,20,39,40,59,60,79,80,99,100}) {
        const FormulaInfo* f = GetFormulaAt(i);
        printf("  %3d  %-22s baseSR=%d\n", i, f->name, f->baseSampleRate);
    }

    printf("\nSanity run, rate 1x, A=B=128, 48000 samples (1 s):\n");
    printf("  idx  %-22s  rms      min      max\n", "name");
    for (int i : {0,10,25,45,50,65,85,99,100}) {
        BytebeatEngine e; e.Init();
        e.SetFormula1(i); e.SetFormula2(i);
        e.SetParamA(128); e.SetParamB(128); e.SetRate(1.0f);
        double rms,mn,mx; run(e,48000,rms,mn,mx);
        printf("  %3d  %-22s  %6.4f  %7.3f  %7.3f\n", i, GetFormulaAt(i)->name, rms, mn, mx);
    }

    // A440 reference: count zero crossings over one second at rate 1x.
    BytebeatEngine e; e.Init();
    e.SetFormula1(GetReferenceIndex()); e.SetParamA(128); e.SetParamB(128); e.SetRate(1.0f);
    int zc=0; float prev=0;
    for(int i=0;i<48000;i++){ float a=0,b=0; e.Process(a,b); if(prev<0&&a>=0) zc++; prev=a; }
    printf("\nA440 reference: %d positive-going zero crossings in 1 s (expect ~440)\n", zc);

    // Param interp grid should not explode or mute.
    BytebeatEngine g; g.Init(); g.SetFormula1(85); g.SetFormula2(45);
    g.SetParamA(137); g.SetParamB(201); g.SetRate(1.0f);
    for (int q : {0,2,8,32}) {
        g.SetParamQuant(q);
        double rms,mn,mx; run(g,4800,rms,mn,mx);
        printf("grid q=%-2d  rms=%6.4f  range %.3f..%.3f\n", q, rms, mn, mx);
    }
    return 0;
}
