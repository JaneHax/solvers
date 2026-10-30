// nonogram solver: exact line DP propagation + marginal-guided DFS with restarts
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#define MAXN 512
#define MAXR 260
int H,W;
int clen[2*MAXN]; int clue[2*MAXN][MAXR];
signed char *g; double *mr,*mc; int *trail; int tn=0;
int dirty[2*MAXN]; int q[2*MAXN]; int qh=0,qt=0,qn=0;
static double F[MAXR+1][MAXN+2],B[MAXR+1][MAXN+2];
unsigned long long rs=88172645463325252ULL;
static inline unsigned long long rnd(){rs^=rs<<13;rs^=rs>>7;rs^=rs<<17;return rs;}
static inline double urand(){return (rnd()>>11)*(1.0/9007199254740992.0);}
static inline int idx(int l,int i){return l<H? l*W+i : i*W+(l-H);}
static void push(int l){ if(!dirty[l]){dirty[l]=1;q[qt]=l;qt=(qt+1)%(2*MAXN);qn++;} }
static void setc(int k,int v){ g[k]=v; trail[tn++]=k; push(k/W); push(H+k%W); }
static int nfill[MAXN+1], nemp[MAXN+1];
static int lineprop(int l){
  int n= l<H?W:H; int m=clen[l]; int *cl=clue[l];
  signed char c[MAXN];
  for(int i=0;i<n;i++) c[i]=g[idx(l,i)];
  nfill[0]=nemp[0]=0;
  for(int i=0;i<n;i++){ nfill[i+1]=nfill[i]+(c[i]==1); nemp[i+1]=nemp[i]+(c[i]==0); }
  #define EMPT(a,b) (nfill[b]-nfill[a]==0)
  #define FILL(a,b) (nemp[b]-nemp[a]==0)
  for(int i=0;i<=n;i++) F[0][i]=EMPT(0,i)?1:0;
  for(int j=1;j<=m;j++){ int L=cl[j-1]; F[j][0]=0;
    for(int i=1;i<=n;i++){ double v=0; if(c[i-1]!=1) v+=F[j][i-1];
      if(i>=L && FILL(i-L,i)){ if(j==1) v+=F[0][i-L]; else if(i-L-1>=0 && c[i-L-1]!=1) v+=F[j-1][i-L-1]; }
      F[j][i]=v; } }
  double tot=F[m][n]; if(!(tot>0)) return 0;
  for(int i=0;i<=n;i++) B[m][i]=EMPT(i,n)?1:0;
  for(int j=m-1;j>=0;j--){ int L=cl[j]; B[j][n]=0;
    for(int i=n-1;i>=0;i--){ double v=0; if(c[i]!=1) v+=B[j][i+1];
      if(i+L<=n && FILL(i,i+L)){ if(j==m-1) v+=B[m][i+L]; else if(i+L<n && c[i+L]!=1) v+=B[j+1][i+L+1]; }
      B[j][i]=v; } }
  double diff[MAXN+1]; int dc[MAXN+1];
  memset(diff,0,sizeof(double)*(n+1)); memset(dc,0,sizeof(int)*(n+1));
  for(int j=0;j<m;j++){ int L=cl[j];
    for(int s=0;s+L<=n;s++){ if(!FILL(s,s+L)) continue;
      double left = j==0? F[0][s] : ((s>=1 && c[s-1]!=1)? F[j][s-1]:0); if(left==0) continue;
      double right = j==m-1? B[m][s+L] : ((s+L<n && c[s+L]!=1)? B[j+1][s+L+1]:0); if(right==0) continue;
      double w=left*right; diff[s]+=w; diff[s+L]-=w; dc[s]++; dc[s+L]--; } }
  double acc=0; int cnt=0; double *M = l<H? mr: mc;
  for(int i=0;i<n;i++){ acc+=diff[i]; cnt+=dc[i];
    int k=idx(l,i);
    if(c[i]!=-1){ M[k]=c[i]; continue; }
    double e=0; for(int j=0;j<=m;j++) if(F[j][i]>0 && B[j][i+1]>0) e+=F[j][i]*B[j][i+1];
    if(e==0 && cnt==0) return 0;
    if(e==0){ setc(k,1); M[k]=1; }
    else if(cnt==0){ setc(k,0); M[k]=0; }
    else { double p=1.0-e/tot; if(p<1e-9)p=1e-9; if(p>1-1e-9)p=1-1e-9; M[k]=p; }
  }
  return 1;
}
static int propagate(){
  while(qn){ int l=q[qh]; qh=(qh+1)%(2*MAXN); qn--; dirty[l]=0;
    if(!lineprop(l)){ while(qn){ dirty[q[qh]]=0; qh=(qh+1)%(2*MAXN); qn--; } return 0; } }
  return 1;
}
static void undo(int to){ while(tn>to){ int k=trail[--tn]; g[k]=-1; push(k/W); push(H+k%W);} }

typedef long double LD;
static LD Fw[MAXR+1][MAXN+2],Bw[MAXR+1][MAXN+2];
double *msgR,*msgC; // msgR[k]: prob filled msg from row line to cell; msgC from col line
static void bpline(int l,double damp){
  int n= l<H?W:H; int m=clen[l]; int *cl=clue[l];
  double *in = l<H? msgC: msgR; double *out = l<H? msgR: msgC;
  LD w0[MAXN+1],w1[MAXN+1]; int kk[MAXN];
  for(int i=0;i<n;i++){ int k=idx(l,i); kk[i]=k; if(g[k]==1){w1[i]=1;w0[i]=0;} else if(g[k]==0){w1[i]=0;w0[i]=1;} else {w1[i]=in[k]; w0[i]=1-in[k];} }
  #define W1(a,b) ({LD _p=1; for(int _t=(a);_t<(b);_t++) _p*=w1[_t]; _p;})
  Fw[0][0]=1; for(int i=1;i<=n;i++) Fw[0][i]=Fw[0][i-1]*w0[i-1];
  for(int j=1;j<=m;j++){ int L=cl[j-1]; Fw[j][0]=0;
    for(int i=1;i<=n;i++){ LD v=w0[i-1]*Fw[j][i-1];
      if(i>=L){ LD sp=W1(i-L,i); if(sp>0){ if(j==1) v+=sp*Fw[0][i-L]; else if(i-L-1>=0) v+=sp*w0[i-L-1]*Fw[j-1][i-L-1]; } }
      Fw[j][i]=v; } }
  LD Z=Fw[m][n]; if(!(Z>0)) return;
  Bw[m][n]=1; for(int i=n-1;i>=0;i--) Bw[m][i]=Bw[m][i+1]*w0[i];
  for(int j=m-1;j>=0;j--){ int L=cl[j]; Bw[j][n]=0;
    for(int i=n-1;i>=0;i--){ LD v=w0[i]*Bw[j][i+1];
      if(i+L<=n){ LD sp=W1(i,i+L); if(sp>0){ if(j==m-1) v+=sp*Bw[m][i+L]; else if(i+L<n) v+=sp*w0[i+L]*Bw[j+1][i+L+1]; } }
      Bw[j][i]=v; } }
  for(int i=0;i<n;i++){ int k=kk[i]; if(g[k]!=-1) continue;
    LD e=0; for(int j=0;j<=m;j++) e+=Fw[j][i]*Bw[j][i+1]; e*=w0[i];
    LD f=Z-e; if(f<0) f=0;
    // belief p = f/Z ; extrinsic odds = (f/e) / (w1/w0)
    double pin=in[k]; double po;
    if(e<=0) po=1-1e-7; else if(f<=0) po=1e-7; else { LD odds=(f/e)*(w0[i]/w1[i]); po=(double)(odds/(1+odds)); }
    if(po<1e-7)po=1e-7; if(po>1-1e-7)po=1-1e-7;
    out[k]=damp*out[k]+(1-damp)*po; (void)pin;
  }
}
static void bpsweep(int it,double damp){ for(int t=0;t<it;t++){ for(int l=0;l<H;l++) bpline(l,damp); for(int l=H;l<H+W;l++) bpline(l,damp);} }
static double belief(int k){ double a=msgR[k],b=msgC[k]; return log(a/(1-a))+log(b/(1-b)); }
int main(int argc,char**argv){
  // input: H W, then H row clues (len then values), then W col clues
  int seed=argc>1?atoi(argv[1]):1; double noise=argc>2?atof(argv[2]):0.0; long maxfail=argc>3?atol(argv[3]):2000;
  rs^=(unsigned long long)seed*0x9E3779B97F4A7C15ULL; for(int i=0;i<10;i++)rnd();
  if(scanf("%d %d",&H,&W)!=2) return 2;
  for(int l=0;l<H+W;l++){ if(scanf("%d",&clen[l])!=1) return 2; for(int j=0;j<clen[l];j++) if(scanf("%d",&clue[l][j])!=1) return 2; }
  int bpit=argc>4?atoi(argv[4]):2; int bpevery=argc>5?atoi(argv[5]):1; int N=H*W; g=malloc(N); msgR=malloc(sizeof(double)*N); msgC=malloc(sizeof(double)*N); for(int k=0;k<N;k++){msgR[k]=msgC[k]=0.5;} mr=malloc(sizeof(double)*N); mc=malloc(sizeof(double)*N); trail=malloc(sizeof(int)*N*2);
  int *lvl=malloc(sizeof(int)*(N+1)); int *dk=malloc(sizeof(int)*(N+1)); signed char *dv=malloc(N+1); signed char *flipped=malloc(N+1);
  clock_t t0=clock(); int restarts=0;
  for(;;){
    memset(g,-1,N); tn=0; for(int l=0;l<H+W;l++) push(l);
    if(!propagate()){ printf("UNSAT\n"); return 1; }
    for(int k=0;k<N;k++){msgR[k]=msgC[k]=0.5;} bpsweep(20,0.3);
    int d=0; long fails=0; double nz=noise+(restarts>0?0.3:0);
    int ok=1;
    for(;;){
      if(d%bpevery==0) bpsweep(bpit,0.3);
      // choose
      int best=-1; double bs=-1;
      for(int k=0;k<N;k++) if(g[k]==-1){
        double lo=belief(k);
        double s=fabs(lo); if(nz>0) s+=nz*urand();
        if(s>bs){bs=s;best=k;}
      }
      if(best<0) break;
      int v = belief(best)>0;
      lvl[d]=tn; dk[d]=best; dv[d]=v; flipped[d]=0; d++;
      setc(best,v);
      while(!propagate()){
        if(fails==0){int u=0;for(int z=0;z<N;z++)u+=g[z]==-1; fprintf(stderr,"first conflict d=%d unknown=%d t=%.1f\n",d,u,(double)(clock()-t0)/CLOCKS_PER_SEC);}
        fails++;
        // backtrack
        for(;;){
          if(d==0){ printf("UNSAT_ROOT\n"); return 1; }
          d--; undo(lvl[d]);
          if(!flipped[d]){ flipped[d]=1; d++; setc(dk[d-1],1-dv[d-1]); break; }
        }
        if(fails>maxfail) break;
      }
      if(fails>maxfail){ ok=0; break; }
    }
    if(ok){
      fprintf(stderr,"solved restarts=%d time=%.2f\n",restarts,(double)(clock()-t0)/CLOCKS_PER_SEC);
      for(int r=0;r<H;r++){ for(int c=0;c<W;c++) putchar(g[r*W+c]==1?'1':'0'); putchar('\n'); }
      return 0;
    }
    {int u=0;for(int z=0;z<N;z++)u+=g[z]==-1; fprintf(stderr,"unknown at restart %d\n",u);}
    restarts++; fprintf(stderr,"restart %d depth %d time %.1f\n",restarts,d,(double)(clock()-t0)/CLOCKS_PER_SEC);
    undo(0); while(qn){dirty[q[qh]]=0;qh=(qh+1)%(2*MAXN);qn--;}
  }
}
