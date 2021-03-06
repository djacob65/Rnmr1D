/*
  ID libCdeconv.cpp
  Copyright (C) 2015-2021 INRAE
  Authors: D. Jacob
*/

// See https://teuder.github.io/rcpp4everyone_en/
// https://knausb.github.io/2017/08/header-files-in-rcpp/

#include <Rcpp.h>
#include <float.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

using namespace Rcpp;

#define TINY  1.0e-20

// Sets the size limits of the different entities
#define COUNT_MAX   256*1024
#define MAXPICS     5000
#define MAXBLOCKS  1500
#define WAVELET_MAX_SIZE    18

// Parameters fixing the lower and upper limits for spectrum processing
// values (in ppm) to be specified in options later
#define WMIN        0.5
#define WMAX        9.5

// Peak/noise ratio
#define RATIOPN     5.0

// Minimal Distance between 2 Peaks
#define MINDISTPK   4

// Filters
#define HAAR        0
#define DAUB2       1
#define DAUB4       2
#define DAUB8       3
#define SYMLET2     4
#define SYMLET4     5
#define SYMLET8     6

#define fNONE        0
#define fDAUB8       1
#define fSYMLET8     2
#define fSAVGOL      3
#define fSMOOTH      4

int _DEBUG_   = 0;
double _PPM1_ = 0;
double _PPM2_ = 0;
int _verbose_ = 1;
int _AJLB_    = 0;

struct s_spectre {
     double *V;
     int    count_max;
     int    LAYER_MAX;
     int    ppm_direct;
     double ppm_max;
     double ppm_min;
     double delta_ppm;
     double B;
};

struct s_peaks {
     int     pics[MAXPICS];
     double  ppm[MAXPICS];
     double  pfac[MAXPICS];
     double  AK[MAXPICS];
     double  sigma[MAXPICS];
     int     d2spmeth;
     int     optim;
     int     optim_int;
     int     optim_sigma;
     int     optim_ppm;
     double  spcv;
     double  d2cv;
     int     d1filt;
     int     d2filt;
     double  RatioPN;
     int     dist_fac;
     double  sigma_min;
     double  sigma_max;
     double  wmin;
     double  wmax;
     double  sigma_moy;
     int     npic;
};

struct s_blocks {
     double  SCMIN;
     int     nstart[MAXBLOCKS];
     int     nstop[MAXBLOCKS];
     double  offset[MAXBLOCKS];
     double  slope[MAXBLOCKS];
     int     np[MAXBLOCKS];
     int     nbblocks;
};

class wavefilt{
public:
    int ncof,ioff,joff;
    double cc[WAVELET_MAX_SIZE];
    double cr[WAVELET_MAX_SIZE];
};

/* ------------------------------------ */
/* numutils                             */
/* ------------------------------------ */

double *vector(int n)
{
    double *v;
    v=(double *) malloc((unsigned) (n+1)*sizeof(double));
    return v;
}

int *ivector(int n)
{
    int *v;
    v=(int *) malloc((unsigned) (n+1)*sizeof(int));
    return v;
}

double **matrix(int l, int c)
{
    int i;
    double *a,**aa;
    a=(double *) malloc((unsigned) (l+1)*(c+1)*sizeof(double));
    aa=(double **) malloc((unsigned) (l+1)*sizeof(double*));
    for (i=0; i<=l; i++) aa[i]=a+l*i;
    return aa;
}

void free_matrix (double **m) { free(m[0]); free(m); }
void free_vector (double *v)  { free(v); }
void free_ivector(int *v)     { free(v); }


double dabs(double x)
{
    return (x>=0) ? x : -x;
}

double dmin(double x, double y)
{
    return (x<y) ? x : y;
}

double dmax(double x, double y)
{
    return (x>y) ? x : y;
}

int comp_qsort(const void *f1, const void *f2)
{ return ( dabs(*(double*)f1) > dabs(*(double*)f2 )) ? 1 : -1; }

double qsort_median( double *a, int n)
{
    qsort(a,n,sizeof(double), comp_qsort);
    return a[n>>1];
}

double median (double *a, int deb, int fin)
{
    int k;
    int bsize = fin - deb + 1;
    double* b = new double[bsize];

    for (k=0;k<bsize;k++) b[k] = a[k+deb];
    return qsort_median(b,bsize);
}

double absmedian (double *a, int deb, int fin)
{
    int k;
    int bsize = fin - deb + 1;
    double* b = new double[bsize];

    for (k=0;k<bsize;k++) b[k] = dabs(a[k+deb]);
    return qsort_median(b,bsize);
}

// Calculation of the derivative (order 4 centered)
void Derive (double *v1, double *v2, int count_max)
{
    int    count;

    for (count=0; count<=count_max; count++) v2[count]=0.0;
    //v2[2]=v1[2]-v1[1]; v2[1]=v2[2];
    //for (count=3; count<=count_max-2; count++)
    //    v2[count]=(v1[count-2]-8*v1[count-1]+8*v1[count+1]-v1[count+2])/12;
    for (count=6; count<=count_max-5; count++)
        v2[count] = (42*(v1[count+1]-v1[count-1]) +
                     48*(v1[count+2]-v1[count-2]) +
                     27*(v1[count+3]-v1[count-3]) +
                      8*(v1[count+4]-v1[count-4]) +
                         v1[count+5]-v1[count-5] )/512;

}

/* ------------------------------------ */
/* LU Decompostion                      */
/* ------------------------------------ */
int LU_decomp(double **a, int n, int *indx, double *d)
{
    int i,imax,j,k;
    double  big,dum,sum,temp;
    double  *vv;

    vv=vector(n);
    *d=1.0;
    for (i=1;i<=n;i++) {
        big=0.0;
        for (j=1;j<=n;j++)
            if ((temp=dabs(a[i][j])) > big) big=temp;
        if (big == 0.0) { return -1; }
        vv[i]=1.0/big;
    }
    imax=0;
    for (j=1;j<=n;j++) {
        for (i=1;i<j;i++) {
            sum=a[i][j];
            for (k=1;k<i;k++) sum -= a[i][k]*a[k][j];
            a[i][j] = sum;
        }
        big=0.0;
        for (i=j;i<=n;i++) {
            sum=a[i][j];
            for (k=1;k<j;k++) sum -= a[i][k]*a[k][j];
            a[i][j] = sum;
            if ( (dum=vv[i]*dabs(sum)) >= big) {
                big=dum;
                imax=i;
            }
        }
        if (j != imax) {
            for (k=1;k<=n;k++) {
                dum=a[imax][k];
                a[imax][k]=a[j][k];
                a[j][k]=dum;
            }
            *d = -(*d);
            vv[imax]=vv[j];
        }
        indx[j]=imax;
        if (a[j][j] == 0.0) a[j][j]=TINY;
        if (j != n) {
            dum=1.0/(a[j][j]);
            for (i=j+1;i<=n;i++) a[i][j] *= dum;
        }
    }
    free_vector(vv);
    return 0;
}

void LU_backsubt(double **a, int n, int *indx, double b[])
{
    int     i,ii=0,ip,j;
    double  sum;

    for (i=1;i<=n;i++) {
        ip=indx[i];
        sum=b[ip];
        b[ip]=b[i];
        if (ii)
            for (j=ii;j<=i-1;j++) sum -= a[i][j]*b[j];
        else if (sum) ii=i;
        b[i]=sum;
    }
    for (i=n;i>=1;i--) {
        sum=b[i];
        for (j=i+1;j<=n;j++) sum -= a[i][j]*b[j];
        b[i]=sum/a[i][i];
    }
}

int LU_resolv(double **a, int n, double b[])
{
    double  d;
    int *indx;
    indx=ivector(n);
    if ( LU_decomp(a,n,indx,&d)<0 ) return -1;
    LU_backsubt(a,n,indx,b);
    free_ivector(indx);
    return 0;
}

/* --------------------------------------- */
/* Savitzky-Golay Filter                   */
/* --------------------------------------- */
void savgolfilt(double *c, int np, int nl, int nr, int ld, int m)
{
    int imj, ipj, j, k, kk, mm, *indx;
    double d, fac, sum, **a, *b;

    if (np<nl+nr+1 || nl<0 || nr<0 || ld>m || nl+nr<m) ::Rf_error("bad args in savgolfilt");
    indx=ivector(m+1);
    a=matrix(m+1,m+1);
    b=vector(m+1);
    for (ipj=0; ipj<=(m<<1); ipj++) {
        sum=(ipj ? 0.0 : 1.0);
        for (k=1; k<=nr; k++) sum += pow((double)k,(double)ipj);
        for (k=1; k<=nl; k++) sum += pow((double)-k,(double)ipj);
        mm = ipj<(2*m-ipj) ? ipj : 2*m-ipj;
        for (imj=-mm; imj<=mm; imj+=2) a[1+(ipj+imj)/2][1+(ipj-imj)/2]=sum;
    }
    LU_decomp(a,m+1,indx,&d);
    for (j=1; j<=m+1; j++) b[j]=0.0;
    b[ld+1]=1.0;
    LU_backsubt(a,m+1,indx,b);
    for (kk=0; kk<np; kk++) c[kk]=0.0;
    for (k=-nl; k<=nr; k++) {
        sum=b[1];
        fac=1.0;
        for (mm=1; mm<=m; mm++) sum+=b[mm+1]*(fac*=k);
        kk=((np-k)%np)+1;
        c[k+nl]=sum;
    }
    free_vector(b);
    free_matrix(a);
    free_ivector(indx);
}

void fsavgol(double *v1, double *v2, int count_max, int m, int nl, int nr)
{
    double *c,*c0,*c1;
    int count,n1,n2,k;
    int     np=nl+nr+1;

    c1=vector(np);
    c0=vector(np);
    savgolfilt(c0,np,nl,nr,0,m);

    for (count=1; count<=count_max; count++) {
        n1=nl; n2=nr; c=c0;
        if (count<nl) {
            n1=count; n2=nr+(nl-count);
            savgolfilt(c1,np,n1,n2,0,m);
            c=c1;
        }
        else if ((count_max-count)<nr) {
            n1=nl+nr-(count_max-count); n2=(count_max-count);
            savgolfilt(c1,np,n1,n2,0,m);
            c=c1;
        }
        v2[count]=0.0;
        for (k=-n1; k<=n2; k++) v2[count] += v1[count+k]*c[n1+k];
    }
    free_vector(c0);
    free_vector(c1);
}

double WinMoy (double *v, int n1, int n2)
{
    int k;
    double  moy=0.0;
    for (k=n1; k<=n2; k++) moy += v[k];
    moy /= (double)(n2-n1+1);
    return moy;
}

void Smooth (double *v1, double *v2, int count_max, int n)
{
    int count,n1,n2;

    for (count=1; count<=count_max; count++) {
        n1 = count >= n ? count - n : 0;
        n2 = count <= count_max - n - 1 ? count + n : count_max - 1;
        v2[count] = WinMoy(v1,n1,n2);
    }
}

/* ------------------------------------ */
/* Wavelet Transform                    */
/* ------------------------------------ */
// * See http://wavelets.pybytes.com/
// * padding so that we have 2^n points
//   https://pywavelets.readthedocs.io/en/latest/ref/signal-extension-modes.html
//   https://www.mathworks.com/help/wavelet/ug/dealing-with-border-distortion.html

std::vector<double>  Haar () {
  int k;
  double coeffs[3] = { 0.0, 1.0, 1.0 };
  std::vector<double> cc(3);
  for (k=1; k < (int)cc.size(); k++) cc[k] = coeffs[k];
  return cc;
}

/* === Daubechies === */

std::vector<double>  daub2 () {
  int k;
  double coeffs[3] = { 0.0,
 7.071067811865475244008443621048490392848359376884740365883398e-01,
 7.071067811865475244008443621048490392848359376884740365883398e-01};
  std::vector<double> cc(3);
  for (k=1; k < (int)cc.size(); k++) cc[k] = coeffs[k];
  return cc;
}

std::vector<double>  daub4 () {
  int k;
  double coeffs[5] = { 0.0,
-1.294095225512603811744494188120241641745344506599652569070016e-01,
 2.241438680420133810259727622404003554678835181842717613871683e-01,
 8.365163037378079055752937809168732034593703883484392934953414e-01,
 4.829629131445341433748715998644486838169524195042022752011715e-01
 };
  std::vector<double> cc(5);
  for (k=1; k < (int)cc.size(); k++) cc[k] = coeffs[k];
  return cc;
}

std::vector<double> daub8 () {
  int k;
  double coeffs[17] = { 0.0,
  -0.00011747678400228192,  0.0006754494059985568,  -0.0003917403729959771, -0.00487035299301066,
   0.008746094047015655,    0.013981027917015516,   -0.04408825393106472,   -0.01736930100202211,
   0.128747426620186,       0.00047248457399797254, -0.2840155429624281,    -0.015829105256023893,
   0.5853546836548691,      0.6756307362980128,      0.3128715909144659,     0.05441584224308161
  };
  std::vector<double> cc(17);
  for (k=1; k < (int)cc.size(); k++) cc[k] = coeffs[k];
  return cc;
}


/* ==== Symlets ==== */

std::vector<double>  symlet2 () {
  int k;
  double coeffs[5] = { 0.0,
  -0.12940952255092145,0.22414386804185735,0.83651630373746899,0.48296291314469025
  };
  std::vector<double> cc(5);
  for (k=1; k < (int)cc.size(); k++) cc[k] = coeffs[k];
  return cc;
}

std::vector<double> symlet4 () {
  int k;
  double coeffs[9] = { 0.0,
  -0.075765714789273325,-0.02963552764599851,0.49761866763201545,0.80373875180591614,
  0.29785779560527736,-0.099219543576847216,-0.012603967262037833,0.032223100604042702
  };
  std::vector<double> cc(9);
  for (k=1; k < (int)cc.size(); k++) cc[k] = coeffs[k];
  return cc;
}

std::vector<double> symlet8 () {
  int k;
  double coeffs[17] = { 0.0,
 -0.0033824159510061256,-0.00054213233179114812,0.031695087811492981, 0.0076074873249176054,
 -0.14329423835080971,  -0.061273359067658524,  0.48135965125837221,  0.77718575170052351,
  0.3644418948353314,   -0.051945838107709037, -0.027219029917056003, 0.049137179673607506,
  0.0038087520138906151,-0.014952258337048231, -0.0003029205147213668,0.0018899503327594609
  };
  std::vector<double> cc(17);
  for (k=1; k < (int)cc.size(); k++) cc[k] = coeffs[k];
  return cc;
}

void Partial_WT(double *a, unsigned long n, wavefilt* wfilt, int isign)
{
    double      ai,ai1;
    int         k;
    unsigned long   i,ii,j,jf,jr,n1,ni,nj,nh,nmod;
    double*     wksp = new double[n+1];

    if (n < 4) return;

    nmod=wfilt->ncof*n;
    n1=n-1;
    nh=n >> 1;

    for(j=1;j<=n;j++) wksp[j]=0.0;
    if (isign>=0) {
        for (ii=1,i=1;i<=n;i+=2,ii++) {
            ni=i+nmod+wfilt->ioff;
            nj=i+nmod+wfilt->joff;
            for (k=1;k<=wfilt->ncof;k++) {
                jf=n1 & (ni+k);
                jr=n1 & (nj+k);
                wksp[ii]    += wfilt->cc[k] * a[jf];
                wksp[ii+nh] += wfilt->cr[k] * a[jr];
            }
        }
    }
    else {
        for (ii=1,i=1;i<=n;i+=2,ii++) {
            ai =a[ii-1];
            ai1=a[ii+nh-1];
            ni=i+nmod+wfilt->ioff;
            nj=i+nmod+wfilt->joff;
            for (k=1;k<=wfilt->ncof;k++) {
                jf=(n1 & (ni+k))+1;
                jr=(n1 & (nj+k))+1;
                wksp[jf] += wfilt->cc[k] * ai;
                wksp[jr] += wfilt->cr[k] * ai1;
            }
        }
    }
    for (j=1;j<=n;j++) a[j-1] = wksp[j];
    delete [] wksp;
}

void WT(double *a, unsigned long n, int isign, std::vector<double> (*fn)())
{
    int k;
    double sig = -1.0;
    unsigned long   nn;
    wavefilt* wfilt = new wavefilt();

    std::vector<double> cc = (*fn)();
    wfilt->ncof = cc.size()-1;
    for (k=1;k<=wfilt->ncof;k++) wfilt->cc[k]=cc[k];

    if (n < 4) return;
    for (k=1;k<=wfilt->ncof;k++) {
        wfilt->cr[wfilt->ncof-k+1] = sig*wfilt->cc[k];
        sig = -sig;
    }
     wfilt->ioff = wfilt->joff = - (wfilt->ncof >> 1);

    if (isign >= 0)
        for (nn=n;nn>=(unsigned long)wfilt->ncof;nn>>=1) Partial_WT(a,nn,wfilt,isign);
    else
        for (nn=wfilt->ncof;nn<=n;nn<<=1) Partial_WT(a,nn,wfilt,isign);
}

void Filtre_layer (double *v1, int count_max, int l1, int l2, int isign)
{
    int    j,k,n1,n2;
    int    layer_max=(int)(log(count_max)/log(2)+0.5);

    for (j=1;j<=layer_max;j++) {
        if (isign>=0 && j>=l1 && j<=l2) continue;
        if (isign<0 && (j<l1 || j>l2))  continue;
        n1=pow(2,j-1); n2=pow(2,j)-1;
        for (k=n1;k<=n2;k++) v1[k]=0;
    }
}

void Filtre_WT (double *v1, int count_max, int l1, int l2, int isign, std::vector<double> (*fn)())
{
    WT(v1,count_max,1,fn);
    Filtre_layer (v1, count_max, l1, l2, isign);
    WT(v1,count_max,-1,fn);
}

void Filtre_Power_WT(double *v1, int count_max, double threshold, std::vector<double> (*fn)())
{
    int    j,k,n1,n2;
    int    layer_max=(int)(log(count_max)/log(2)+0.5);
    double *P,Pj,S;
    S=0;
    WT(v1,count_max,1,fn);
    P=vector(layer_max);
    for (j=1;j<=layer_max;j++) {
        n1=pow(2,j-1); n2=pow(2,j)-1;
        P[j]=0;
        for (k=n1;k<=n2;k++) P[j] += v1[k]*v1[k];
        S += P[j];
    }
    if(_verbose_>1) Rcout << " Zeroing layers : ";
    for (j=1;j<=layer_max;j++) {
        n1=pow(2,j-1); n2=pow(2,j)-1;
        Pj = (100.0*P[j]/S)/(n2-n1+1);
        if (Pj<threshold/count_max) {
            if(_verbose_>1) Rcout << j << " ";
            for (k=n1;k<=n2;k++) v1[k]=0;
        }
    }
    free_vector(P);
    WT(v1,count_max,-1,fn);
}

void filtsigbywt (double *v1, double *v2, int count_max, double threshold, std::vector<double> (*fn)())
{
    double  d1[COUNT_MAX];
    int     count;

    for (count=0; count<=count_max; count++) d1[count]=v1[count];
    Filtre_Power_WT (d1,count_max,threshold,fn);
    for (count=0; count<=count_max; count++) v2[count]=d1[count];
}

void wt2fn(double *a, unsigned long n, int isign, int wavelet)
{
   switch(wavelet) {
      case HAAR:
           WT(a,n,isign,Haar);
           break;
      case DAUB2:
           WT(a,n,isign,daub2);
           break;
      case DAUB4:
           WT(a,n,isign,daub4);
           break;
      case DAUB8:
           WT(a,n,isign,daub8);
           break;
      case SYMLET2:
           WT(a,n,isign,symlet2);
           break;
      case SYMLET4:
           WT(a,n,isign,symlet4);
           break;
      case SYMLET8:
           WT(a,n,isign,symlet8);
           break;
   }
}

/* ------------------------------------ */
/* Optimization by the gradient method  */
/* ------------------------------------ */

void florentz(double x, double a[], double *y, double dyda[], int na)
{
    int i, np;
    double U,S,U2,S2,V,V2;

    *y=0.0;
    np=(_AJLB_ >0) ? na - _AJLB_ - 1 : na;
    for (i=1;i<=np-2;i+=3) {
        U=x-a[i+1]; S=a[i+2]; U2=U*U; S2=S*S; V=U2+S2; V2=V*V;
        dyda[i]=S2/V;
        dyda[i+1]=2*a[i]*U*S2/V2;
        dyda[i+2]=2*a[i]*U2*S/V2;
        *y += a[i]*dyda[i];
    }
    if (_AJLB_ >0) {
       double xp = 1;
       dyda[np+1]=1;
       for(i=1; i<=_AJLB_; i++) { *y += a[np+i+1]*xp; dyda[np+i+1]=0.01*xp; xp *= (x-a[np+1]); }
    }
}

void mrqcof( double x[], double y[], double sig[], int ndata, double a[], int ia[], int ma, double **alpha, double beta[], double *chisq )
{
    int i,j,k,l,m,mfit=0;
    double ymod,wt,sig2i,dy,*dyda;
    dyda=vector(ma);
    for (j=1;j<=ma;j++)
        if (ia[j]) mfit++;
    for (j=1;j<=mfit;j++) {
        for (k=1;k<=j;k++) alpha[j][k]=0.0;
        beta[j]=0.0;
    }
    *chisq=0.0;
    for (i=1;i<=ndata;i++) {
        florentz(x[i],a,&ymod,dyda,ma);
        sig2i=1.0/(sig[i]*sig[i]);
        dy=y[i]-ymod;
        for (j=0,l=1;l<=ma;l++) {
            if (ia[l]) {
                wt=dyda[l]*sig2i;
                for(j++,k=0,m=1;m<=l;m++)
                    if (ia[m]) alpha[j][++k]  += wt*dyda[m];
                beta[j] += dy*wt;
            }
        }
        *chisq += dy*dy*sig2i;
    }
    for (j=2;j<=mfit;j++)
        for(k=1;k<j;k++) alpha[k][j]=alpha[j][k];
    free_vector(dyda);
}

int mrqmin( double x[], double y[], double sig[], int ndata, double a[], int ma, int ia[], double **covar, double **alpha, double *chisq, double *alamda )
{
    int j,k,l;
    static int mfit;
    static double ochisq,*atry,*beta,*da;

    if (*alamda <0.0) {
        atry=vector(ma);
        da=vector(ma);
        beta=vector(ma);
        for (mfit=0,j=1;j<=ma;j++)
            if (ia[j]) mfit++;
        *alamda=1.0;
        mrqcof(x,y,sig,ndata,a,ia,ma,alpha,beta,chisq);
        ochisq=(*chisq);
        for (j=1;j<=ma;j++) atry[j]=a[j];
    }
    for (j=1;j<=mfit;j++){
        for (k=1;k<=mfit;k++)
            covar[j][k] =alpha[j][k];
        covar[j][j]=alpha[j][j]*(1.0 +(*alamda));
    }
    if ( LU_resolv(covar,mfit,beta)<0 ) return -1;
    for (j=1;j<=mfit;j++)
        da[j]=beta[j];
    if (*alamda==0.0) {
        free_vector(beta);
        free_vector(da);
        free_vector(atry);
        return 0;
    }
    for (j=0,l=1;l<=ma;l++)
        if (ia[l]) atry[l]=a[l]+da[++j];
    mrqcof(x,y,sig,ndata,atry,ia,ma,covar,da,chisq);
    if (*chisq < ochisq) {
        *alamda *= 0.1;
        ochisq=(*chisq);
        for (j=1;j<=mfit;j++) {
            for (k=1;k<=mfit;k++) alpha[j][k]=covar[j][k];
            beta[j]=da[j];
        }
        for (l=1;l<=ma;l++) a[l]=atry[l];
    } else {
        *alamda *= 10.0;
        *chisq=ochisq;
    }
    return 0;
}

int optimize(double x[], double y[], int ndata, double a[], int ia[], int na)
{
    double *sig, **covar, **alpha;
    double chi2,chisq,alamda,olamda;

    int i,maxstep,step;

    sig=vector(ndata); for(i=1;i<=ndata;i++) { sig[i]=1.0; }
    covar=matrix(na,na); alpha=matrix(na,na);

    /* first step : alamda=-1 : init */
    alamda=-1.0;
    if ( mrqmin(x,y,sig,ndata,a,na,ia,covar,alpha,&chisq,&alamda)<0 ) { free_matrix(alpha); free_matrix(covar); free_vector(sig); return -1; }
    chi2=chisq;
    maxstep=50;
    step=0;
    olamda=alamda;

    /* optim loop */
    while ( (alamda>0 && alamda<=olamda && (chisq/chi2)>0.0) && step<maxstep) {
        chi2=chisq;
        olamda=alamda;
        if ( mrqmin(x,y,sig,ndata,a,na,ia,covar,alpha,&chisq,&alamda)<0 ) { free_matrix(alpha); free_matrix(covar); free_vector(sig); return -1; }
        if (((chi2-chisq)/chi2)<0.005) break;
        step++;
    }

    /* free mem */
    alamda=0.0;
    if ( mrqmin(x,y,sig,ndata,a,na,ia,covar,alpha,&chisq,&alamda)<0 ) { free_matrix(alpha); free_matrix(covar); free_vector(sig); return -1; }
    free_matrix(alpha); free_matrix(covar); free_vector(sig);
    return 0;
}



/* =========================================================================
       Main functions internally called
   =========================================================================*/

/* ------------------------------------ */
/* Peak Finding and Selection           */
/* ------------------------------------ */

// [[Rcpp::export]]
double lorentz(double x,double x0, double s) {  return s*s/(s*s+(x-x0)*(x-x0)); }

double ppmval(struct s_spectre *sp, double count)
{
    return (sp->ppm_direct) ?
         sp->ppm_min + (count-1)*sp->delta_ppm :
         sp->ppm_max - (count-1)*sp->delta_ppm;
}

int cntval(struct s_spectre *sp, double ppm)
{
    return (sp->ppm_direct) ?
         (int)((ppm - sp->ppm_min)/sp->delta_ppm + 1) :
         (int)((sp->ppm_max - ppm)/sp->delta_ppm + 1);
}

// Peak Finding - Maxima method applied to the spectrum + Minima method applied to the second derivation
void find_peaks (struct s_spectre *sp, struct s_peaks *pk)
{

    double  d1[COUNT_MAX],d2[COUNT_MAX];
    double  vp[COUNT_MAX];
    double dy1,dy2,fac1,fac2;
    int k,delta,count, cpm1, cpm2;

    for (count=0; count<=sp->count_max; count++) vp[count]=0.0;

    // Maxima method applied to the spectrum
    if (_verbose_>1) Rprintf("\tMaximum Method on sp ... ");
    fac1 = (pk->spcv>0) ? pk->spcv : 0.005;
    cpm1=0;
    for (count=100; count<sp->count_max-100; count++) {
        if (ppmval(sp,count) < pk->wmin || ppmval(sp,count) > pk->wmax) continue;
        if ( sp->V[count]  >sp->V[count-1] && sp->V[count]  >sp->V[count+1] &&
             sp->V[count-1]>sp->V[count-2] && sp->V[count+1]>sp->V[count+2] &&
             sp->V[count-2]>sp->V[count-3] && sp->V[count+2]>sp->V[count+3] ) {
             if ( sp->V[count] < sp->B ) continue;
             dy1=sp->V[count]-sp->V[count-3]; dy2=sp->V[count]-sp->V[count+3];
             if (dy1>dy2 && dy1/sp->V[count] < fac1 ) continue;
             if (dy2>dy1 && dy2/sp->V[count] < fac1 ) continue;
if (_DEBUG_ && ppmval(sp,count)>_PPM1_ && ppmval(sp,count)<_PPM2_ ) Rprintf("M1: %d, %f \n",count, ppmval(sp,count));
             vp[count]=sp->V[count];
             cpm1++;
        }
    }
    if (_verbose_>1) Rprintf("%d peaks found. OK\n",cpm1);

    //Minima method applied to the second derivation
    if (pk->d2spmeth>0) {

        //-------- first derivative -----------------------------------
        Derive(sp->V,d1,sp->count_max);
        if (pk->d1filt)
           Filtre_WT(d1, sp->count_max, sp->LAYER_MAX, sp->LAYER_MAX, -1,daub8);

        //-------- second derivative ------------------------------------
        Derive(d1,d2,sp->count_max);
        if (pk->d2filt)
           Filtre_WT(d1, sp->count_max, sp->LAYER_MAX, sp->LAYER_MAX, -1,daub8);

       if (_verbose_>1) Rprintf("\tMinimum Method on d2sp ... ");
       fac2= (pk->d2cv>0) ? pk->d2cv : 0.05;
       cpm2=0;
       for (count=100; count<sp->count_max-100; count++) {
           if (ppmval(sp,count) < pk->wmin || ppmval(sp,count) > pk->wmax) continue;
           delta = (int)(pk->dist_fac*pk->sigma_min/sp->delta_ppm);
           for(k=count-delta; k<=count+delta; k++) if (vp[k]>0) break;
           if (vp[k]>0) continue;
           for(k=count-2; k<=count+2; k++) if (d2[k]>0) break;
           if (d2[count]<d2[count-1] && d2[count]<d2[count+1] &&
               d2[count-1]<d2[count-2] && d2[count+1]<d2[count+2]) {
                  if (dabs((d2[count] - 0.5*(d2[count+2]+d2[count-2]))/d2[count]) < fac2 ) continue;
                  if (sp->V[count] - 0.5*(sp->V[count-1]+sp->V[count+1]) < 1.0*sp->B ) continue;
if (_DEBUG_ && ppmval(sp,count)>_PPM1_ && ppmval(sp,count)<_PPM2_ ) Rprintf("M2: %d, %f \n",count, ppmval(sp,count));
                  vp[count] = sp->V[count];
                  cpm2++;
           }
       }
       if (_verbose_>1) Rprintf("%d peaks found. OK\n",cpm2);

    }

    if (_verbose_>1) Rprintf("\tSave peaks ... ");
    pk->npic=0;
    for (count=1; count<=sp->count_max; count++) {
        if (ppmval(sp,count) < pk->wmin || ppmval(sp,count) > pk->wmax) continue;
        if (vp[count]>0) {
           pk->pfac[pk->npic] = 0;
           if (sp->V[count]>sp->V[count-1] && sp->V[count]>sp->V[count+1]) {
               dy1=sp->V[count]-sp->V[count-1]; dy2=sp->V[count]-sp->V[count+1];
               if (dy2<dy1)
                  pk->pfac[pk->npic] = 0.5*(1-dy2/dy1);
               else
                  pk->pfac[pk->npic] = 0.5*(dy1/dy2-1);
           }
           pk->pics[pk->npic] = count;
           pk->ppm[pk->npic]=ppmval(sp,pk->pics[pk->npic]+pk->pfac[pk->npic]);
if (_DEBUG_ && ppmval(sp,count)>_PPM1_ && ppmval(sp,count)<_PPM2_ ) Rprintf("SP: %d, %f \n",count, pk->ppm[pk->npic]);
           pk->AK[pk->npic] = 0.95*vp[count];
           pk->sigma[pk->npic] = pk->sigma_min/sp->delta_ppm;
           pk->npic++;
           if (pk->npic==(MAXPICS-1)) ::Rf_error("Max peak count has been reached");
        }
    }
    if (_verbose_>1) Rprintf("OK\n");
}

// Peak Selection
void select_peaks(struct s_spectre *sp, struct s_peaks *pk)
{
   int k,select_npic;
   select_npic=0;
   for (k=0;k<pk->npic;k++) {
      if (pk->pics[k]<0 || pk->pics[k]>sp->count_max) continue;
      if (pk->AK[k] < 0.0 ) continue;
      if (pk->AK[k] != pk->AK[k]) continue; // test if NaN
      if (pk->AK[k] < pk->RatioPN*sp->B) continue;
      if ((pk->pics[k+1]-pk->pics[k])<MINDISTPK) {
          if (pk->AK[k+1]>pk->AK[k]) continue;
          pk->AK[k+1]=0;
      }
      if (select_npic < k) {
         pk->AK[select_npic]=dmin(pk->AK[k],sp->V[pk->pics[k]]);
         pk->sigma[select_npic]=pk->sigma[k];
         pk->pics[select_npic]=pk->pics[k];
         pk->ppm[select_npic]=pk->ppm[k];
         pk->pfac[select_npic]=pk->pfac[k];
      }
      select_npic++;
   }
   pk->npic = select_npic;
}

/* ------------------------------------ */
/* Estimation of Sigmas                 */
/* ------------------------------------ */

// Estimation of sigmas based on the second derivative
void estime_sigma(struct s_spectre *sp,struct s_peaks *pk)
{
    int k,i,j,n,loop;
    double x1,x2;
    double  d1[COUNT_MAX],d2[COUNT_MAX];

    //-------- first derivative -----------------------------------
    Derive(sp->V,d1,sp->count_max);
    if (pk->d1filt)
       Filtre_WT(d1, sp->count_max, sp->LAYER_MAX, sp->LAYER_MAX, -1,daub8);

    //-------- second derivative ------------------------------------
    Derive(d1,d2,sp->count_max);
    if (pk->d2filt)
       Filtre_WT(d1, sp->count_max, sp->LAYER_MAX, sp->LAYER_MAX, -1,daub8);

    for (k=1; k<pk->npic; k++) {
        n = pk->pics[k];
             if (k>1 && d2[n-2]<d2[n]) n=n-2;
        else if (k>0 && d2[n-1]<d2[n]) n--;
        else if (k<pk->npic && d2[n+1]<d2[n]) n++;
        else if (k<pk->npic-1 && d2[n+2]<d2[n]) n=n+2;
        // Find the zeros of the 2nd derivative
        loop=1; i=0;
        while (loop) {
           i++;
           if (d2[n-i]<0) continue;
           loop=0;
           x1 = n-i+1 + d2[n-i+1]/(d2[n-i]-d2[n-i+1]);
        }
        loop=1; j=0;
        while (loop) {
           j++;
           if (d2[n+j]<0) continue;
           loop=0;
           x2 = n+j-1 + d2[n+j-1]/(d2[n+j]-d2[n+j-1]);
        }
        // Estimation of sigma
        pk->sigma[k]=dmax(pk->sigma_min/sp->delta_ppm, sqrt(3)*(x2-x1)/2);
    }
    pk->sigma_moy=0.0;
    for (k=0; k<pk->npic; k++) pk->sigma_moy+=pk->sigma[k];
    pk->sigma_moy /= pk->npic;
}

/* ------------------------------------ */
/* Estimation of AK                     */
/* ------------------------------------ */

// Estimation of AK based on the first derivative
void estime_AK(struct s_spectre *sp,struct s_peaks *pk)
{
  int k,n,i,j;
  double v1,v2,dL1,dL2;

  //-------- First Derivative -----------------------------------
  double  d1[COUNT_MAX];
  Derive(sp->V,d1,sp->count_max);

  for (k=0; k<pk->npic; k++)
  {
      n = pk->pics[k];
      if (k>1 && sp->V[n-2]>sp->V[n]) n=n-2;
      else if (k>0 && sp->V[n-1]>sp->V[n]) n--;
      else if (k<pk->npic && sp->V[n+1]>sp->V[n]) n++;
      else if (k<pk->npic-1 && sp->V[n+2]>sp->V[n]) n=n+2;
      if ( sp->V[n]>sp->V[n-1] && sp->V[n]>sp->V[n+1] && sp->V[n-1]>sp->V[n-2] && sp->V[n+1]>sp->V[n+2] )
      {
         i=0; if ((sp->V[n]-sp->V[n-1])/sp->V[n] < pk->spcv) i++;
         j=0; if ((sp->V[n]-sp->V[n+1])/sp->V[n] < pk->spcv) j++;
         i++; j++;
         while( (i<5 || j<5) && sp->V[n-i-1]<sp->V[n-i] && sp->V[n+j+1]<sp->V[n+j] ) { i++; j++; }
         v1=(n-i-pk->pics[k]-pk->pfac[k])/pk->sigma[k];
         v2=(n+j-pk->pics[k]-pk->pfac[k])/pk->sigma[k];
         dL1=-2*v1/(pk->sigma[k]*(1+v1*v1)*(1+v1*v1));
         dL2=-2*v2/(pk->sigma[k]*(1+v2*v2)*(1+v2*v2));
         pk->AK[k] = dmin( 0.95*sp->V[n], (d1[n+j]-d1[n-i])/(dL2-dL1) );
      } else pk->AK[k] = 0;
  }
}

// Estimation of AK based on demixing (by resolution of linear systems)
void estime_AK2(struct s_spectre *sp,struct s_peaks *pk)
{
    double **aa,*vv,yk;
    int i,j,k,m,l,np,ind, indk, xk;
    int indx[MAXPICS];


    for (k=0; k<pk->npic; k++) {
        xk = pk->pics[k];  yk = sp->V[xk];
        pk->AK[k] = yk;
        ind=0; m=1;
        while ((k-m)>=0 && m<=50) {
            double ykm=lorentz(xk,pk->pics[k-m],pk->sigma[k-m]);
            if (ykm>0.01) indx[ind++]=k-m;
            m++;
        }

        indk=ind;
        indx[ind++]=k;

        l=1;
        while ((k+l)<=pk->npic && l<=50) {
            double ykl=lorentz(xk,pk->pics[k+l],pk->sigma[k+l]);
            if (ykl>0.01) indx[ind++]=k+l;
            l++;
        }
        if (ind==1) continue;

        np=ind;
        aa=matrix(np,np);
        vv=vector(np);

        for(i=1;i<=np;i++)
            for(j=1; j<=np; j++) {
                if (i==j) {  aa[i][j]=1.0; continue; }
                aa[i][j]=lorentz(pk->pics[indx[i-1]],pk->pics[indx[j-1]],pk->sigma[indx[j-1]]);
            }
        for (i=1;i<=np;i++) { vv[i]=sp->V[pk->pics[indx[i-1]]]; }
        LU_resolv(aa,np,vv);
        pk->AK[k]=vv[indk+1]>0 ? vv[indk+1] : -vv[indk+1];

        free_vector(vv);
        free_matrix(aa);
    }
}

/* ------------------------------------ */
/* Optimization of Amplitudes & Sigmas  */
/* ------------------------------------ */

// Parameters for the block spectrum division in order to optimize the peaks in packets ("blocks"):
// computation time proportional to the inversion time of the matrix n*n (O(nlogn)),
// with n = number of peaks in the "block" */
// SCMIN : minimum distance (as a multiple of sigma) between two peaks

void optim_peaks(struct s_spectre *sp,struct s_peaks *pk,struct s_blocks *blocks)
{
    double  *Xw,*Yw,*aw,diff_n,som_s,som_p,pmin,pmax,fmin,ac;
    int     *iaw;
    int i,j,k,l,m,n,np,na,som_np,ndata,nstart,nstop;

    blocks->nbblocks=0;

    if(_verbose_>1) Rprintf("\t #:  interval ppm\t#pts\t#peaks\tIntensity\n");
    if(_verbose_>1) Rprintf("\t----------------------------------------------------\n");

    k=0; m=1; np=som_p=som_np=nstart=nstop=0;
    while (k<pk->npic) {

        // Start of the Block
        if (k==0) {
            fmin=sp->V[pk->pics[k]];
            for (n=pk->pics[k]-2; n>(pk->pics[k]-8*blocks->SCMIN*pk->sigma[k]); n--) {
                if (n==1) { nstart=1; break; }
                if (sp->V[n]<fmin) { nstart=n; fmin=sp->V[n]; }
            }
        }
        else
            nstart=nstop;

         // End of the Block
        l=0;
        while ((k+l)<pk->npic) {
            if ((k+l)==(pk->npic-1)) {
               fmin=sp->V[pk->pics[k+l]];
               for (n=pk->pics[k+l]+2; n<=(pk->pics[k+l]+8*blocks->SCMIN*pk->sigma[k+l]); n++) {
                    if (n==sp->count_max) { nstop=n-1; break; }
                    if (sp->V[n]<fmin) { nstop=n; fmin=sp->V[n]; }
               }
               break;
            }
            diff_n = pk->pics[k+l+1]  - pk->pics[k+l];
            som_s  = pk->sigma[k+l+1] + pk->sigma[k+l];
            if (diff_n >= blocks->SCMIN*som_s) {
                   fmin=dmax( sp->V[pk->pics[k+l]], sp->V[pk->pics[k+l+1]] );
                   for (n=pk->pics[k+l]+2; n<=pk->pics[k+l+1]-2; n++)
                       if (sp->V[n]<fmin) { nstop=n; fmin=sp->V[n]; }
                   break;
            }
            l++;
        }
        np=l+1;
        ndata=(nstop-nstart+1);
        blocks->offset[blocks->nbblocks]=0;
        blocks->slope[blocks->nbblocks]=0;

        if (pk->optim) {
            // Data extraction Y=f(X) between nstart and nstop
            Xw=vector(ndata); Yw=vector(ndata);
            for (j=1; j<=ndata; j++) {
                Xw[j]=ppmval(sp,nstart+j-1);
                Yw[j]=sp->V[nstart+j-1];
            }
            // Initial value of the parameters
            na=(_AJLB_>0) ? np*3 + _AJLB_ + 1: np*3;
            aw=vector(na); iaw=ivector(na);
            for (i=1; i<=np; i++) {
                j=3*i-2;
                ac =  pk->AK[k+i-1];
                if (ac < 0.0 ) ac = -ac;
                aw[j]  =ac;
                aw[j+1]=pk->ppm[k+i-1];
                aw[j+2]=pk->sigma[k+i-1]*sp->delta_ppm;
                iaw[j]=pk->optim_int; iaw[j+1]=pk->optim_ppm; iaw[j+2]=pk->optim_sigma;
            }
            if (_AJLB_ >0) {
                aw[np+1]=0.5*(ppmval(sp,nstop) + ppmval(sp,nstart)); iaw[np+1]=0;
                aw[np+2]=0.05*(sp->V[nstop] + sp->V[nstart]);
                iaw[np+2]=1;
            }
            if (_AJLB_ >1) {
                aw[np+3]=0.01*(sp->V[nstop] - sp->V[nstart])/(ppmval(sp,nstop) - ppmval(sp,nstart));
                iaw[np+3]=1;
            }
            if (_AJLB_ >2) {
                for(i=3; i<=_AJLB_; i++) { aw[np+i+1]=0.01; iaw[np+i+1]=1; }
            }

            // Optimize ak, sk, pk
            optimize(Xw,Yw,ndata,aw,iaw,na);

            // Recovers the new values of the parameters and calculates the intensity of each peak in the block/bunch
            som_p=0;
            for (i=1; i<=np; i++) {
                j=3*i-2;
                if (aw[j+2]>pk->sigma_max)   aw[j+2]=pk->sigma_max;
                if (aw[j+2]<pk->sigma_min)   aw[j] = 0.0;
                if (aw[j]<pk->RatioPN*sp->B) aw[j] = 0.0;

                pk->AK[k+i-1] = aw[j];
                pk->sigma[k+i-1] = aw[j+2]/sp->delta_ppm;
                if (pk->optim_ppm) {
                    pk->ppm[k+i-1] = aw[j+1];
                    pk->pics[k+i-1] = cntval(sp,aw[j+1]);
                }
                som_np++;
                som_p += M_PI*pk->AK[k+i-1]*pk->sigma[k+i-1];
            }
            if (_AJLB_ >0 ) blocks->offset[blocks->nbblocks] = aw[np+2];
            if (_AJLB_ >1 ) blocks->slope[blocks->nbblocks]  = aw[np+3];
            free_ivector(iaw);
            free_vector(aw);
            free_vector(Yw);
            free_vector(Xw);
        }
        pmin = ppmval(sp,nstart);
        pmax = ppmval(sp,nstop);

        blocks->nstart[blocks->nbblocks]=nstart;
        blocks->nstop[blocks->nbblocks]=nstop;
        blocks->np[blocks->nbblocks]=np;
        blocks->nbblocks++;

        if(_verbose_>1) Rprintf("\t%2d: %f- %f\t%d\t%d",blocks->nbblocks,pmin,pmax,ndata,np);
        if(_verbose_>1) Rprintf("\t%f\n",som_p);
        if (blocks->nbblocks==MAXBLOCKS) ::Rf_error("Max block count has been reached");
        m++; k += (l+1);
    }

    if(_verbose_>1) Rprintf("\t----------------------------------------------------\n");
    if(_verbose_>0) Rprintf("\tNb blocks = %d\tNb significant peaks = %d\n",blocks->nbblocks,som_np);

}

/* =========================================================================
       Main functions externally called
   =========================================================================*/

/* ------------------------------------ */
/* Denoising  */
/* ------------------------------------ */

// [[Rcpp::export]]
SEXP C_fSavGol (SEXP s, int m, int nl, int nr)
{
   NumericVector Y(s);
   double  v1[COUNT_MAX], v2[COUNT_MAX];
   int k;

   // Note: Index translation  from range[0 - N-1] to range[1 - N]
   for (k=0; k<Y.size(); k++) v1[k+1]=Y[k];

   fsavgol(v1, v2, Y.size(), m, nl, nr);

   NumericVector Yf(Y.size());
   for (k=0; k<Y.size(); k++) Yf[k]=v2[k+1];

   return(Yf);
}

// [[Rcpp::export]]
SEXP C_FilterbyWT (SEXP s, int type, double threshold = 0.5, int verbose=0)
{
   NumericVector Y(s);
   double  v1[COUNT_MAX];

   int k;
   int N = Y.size();

   // Note: Index translation  from range[0 - N-1] to range[1 - N]
   for (k=0; k<N; k++) v1[k+1]=Y[k];

   _verbose_=verbose;
   switch(type) {
      case HAAR:
           Filtre_Power_WT (v1,N,threshold,Haar);
           break;
      case DAUB2:
           Filtre_Power_WT (v1,N,threshold,daub2);
           break;
      case DAUB4:
           Filtre_Power_WT (v1,N,threshold,daub4);
           break;
      case DAUB8:
           Filtre_Power_WT (v1,N,threshold,daub8);
           break;
      case SYMLET2:
           Filtre_Power_WT (v1,N,threshold,symlet2);
           break;
      case SYMLET4:
           Filtre_Power_WT (v1,N,threshold,symlet4);
           break;
      case SYMLET8:
           Filtre_Power_WT (v1,N,threshold,symlet8);
           break;
   }

   NumericVector Yf(Y.size());
   for (k=0; k<Y.size(); k++) Yf[k]=v1[k+1];

   return(Yf);
}

// [[Rcpp::export]]
SEXP C_FilterbyThreshold (SEXP s, int wavelet, int threshold=0)
{
   NumericVector Y(s);
   double  v1[COUNT_MAX];

   double lambda,m;
   int count,k;
   int N = Y.size();

   // Note: Index translation  from range[0 - N-1] to range[1 - N]
   for (k=0; k<N; k++) v1[k+1]=Y[k];

   wt2fn(v1,N,1,wavelet);

   m = absmedian(v1,N>>2,N-1);
   lambda = (m/0.6745)*sqrt(2*log(N));
   for (count=1; count<=N; count++) {
        double y=v1[count];
        if (dabs(y)<lambda) y=0;
        if (threshold>0 && dabs(y)>lambda)  y -= y > 0 ? lambda : -lambda;
        v1[count]=y;
   }

   wt2fn(v1,N,-1,wavelet);

   NumericVector Yf(Y.size());
   for (k=0; k<Y.size(); k++) Yf[k]=v1[k+1];

   return(Yf);
}

/* ------------------------------------ */
/* Deconvolution  */
/* ------------------------------------ */

// [[Rcpp::export]]
SEXP C_Lorentz(SEXP ppm, double amp, double x0, double sigma)
{
    int k;
    NumericVector VecIn(ppm);
    NumericVector VecOut(VecIn.size());
    for (k=0; k<VecIn.size(); k++)
        if (dabs(VecIn[k]-x0)<100)
           VecOut[k]=amp*sigma*sigma/(sigma*sigma+(VecIn[k]-x0)*(VecIn[k]-x0));
        else
           VecOut[k]=0.0;
    return(VecOut);
}

// [[Rcpp::export]]
SEXP C_OneLorentz(SEXP X, SEXP Y, SEXP par)
{
   NumericVector vx(X);
   NumericVector vy(Y);
   NumericVector vp(par);

   double  *Xw,*Yw,*aw;
   int     *iaw;
   int k, na,ndata;

   ndata=vy.size(); na=3;
   Xw=vector(ndata); Yw=vector(ndata);
   aw=vector(na); iaw=ivector(na);

   for (k=0; k<ndata; k++) { Xw[k+1]=vx[k]; Yw[k+1]=vy[k]; }
   for (k=0; k<na; k++)    { aw[k+1]=vp[k]; iaw[k+1]=1; }
   optimize(Xw,Yw,ndata,aw,iaw,na);

   free_vector(aw);
   free_vector(Yw);
   free_vector(Xw);

   NumericVector vout(na);
   for (k=0; k<na; k++) vout[k]=aw[k+1];

   return(vout);
}


// [[Rcpp::export]]
SEXP C_MyFuncTest(SEXP spec, SEXP ppmrange, Nullable<List> filt = R_NilValue, Nullable<List> peaks = R_NilValue, int verbose=1)
{
    List slist(spec);
    NumericVector Y = as<NumericVector>(slist["int"]);
    NumericVector W(ppmrange);

    struct s_spectre sp;
    struct s_peaks pk;
    struct s_blocks blocks;

    int i,k,fn;
    double  *v1, *v2, ppm;

    v1=vector(COUNT_MAX);
    v2=vector(COUNT_MAX);

    // Note: Index translation  from range[0 - N-1] to range[1 - N]
    for (k=0; k<Y.size(); k++) v1[k+1]=Y[k];
    sp.V = v1;

    sp.count_max = Y.size();
    sp.LAYER_MAX = as<double>(slist["LAYER_MAX"]);
    sp.ppm_max = as<double>(slist["pmax"]);
    sp.ppm_min = as<double>(slist["pmin"]);
    sp.delta_ppm = as<double>(slist["dppm"]);
    sp.B = as<double>(slist["B"]);
    sp.ppm_direct = 1;

    pk.wmin = W[0];
    pk.wmax = W[1];

    _verbose_ = verbose;
    if(_verbose_>0) Rcerr << "----------------------------------------------------" << std::endl;

    if (filt.isNotNull()) {
       List flist(filt);
       fn = as<int>(flist["type"]);
       if(_verbose_>0) Rcout << "Filter = ";
       switch(fn) {
         case fNONE:
            if(_verbose_>0) Rcout << "None";
            for (k=1; k<=Y.size(); k++) v2[k]=v1[k];
            break;
         case fDAUB8:
            if(_verbose_>0) Rcout << "daub8";
            filtsigbywt(sp.V,v2,sp.count_max,as<double>(flist["threshold"]),daub8);
            break;
         case fSYMLET8:
            if(_verbose_>0) Rcout << "symlet8";
            filtsigbywt(sp.V,v2,sp.count_max,as<double>(flist["threshold"]),symlet8);
            break;
         case fSAVGOL:
            if(_verbose_>0) Rcout << "savgol";
            fsavgol(sp.V,v2,sp.count_max, as<int>(flist["m"]), as<int>(flist["nl"]), as<int>(flist["nr"]));
            break;
         case fSMOOTH:
            if(_verbose_>0) Rcout << "smooth";
            Smooth(sp.V,v2,sp.count_max, as<int>(flist["m"]));
            break;
       }
       if(_verbose_>0) Rcout << std::endl;
    } else {
       for (k=1; k<=Y.size(); k++) v2[k]=v1[k];
    }

    List ret;

    NumericVector Yf(Y.size());
    for (k=0; k<Y.size(); k++) Yf[k]=v2[k+1];
    ret["int"] = Yf;

    if (peaks.isNotNull()) {

       List plist(peaks);

       // Get input parameters
       pk.d2spmeth    = plist.containsElementNamed("d2spmeth")  ? as<int>(plist["d2spmeth"]) : 0;
       pk.optim       = plist.containsElementNamed("optim")     ? as<int>(plist["optim"]) : 1;
       pk.optim_int   = plist.containsElementNamed("oa")        ? as<int>(plist["oa"]) : 1;
       pk.optim_sigma = plist.containsElementNamed("os")        ? as<int>(plist["os"]) : 1;
       pk.optim_ppm   = plist.containsElementNamed("op")        ? as<int>(plist["op"]) : 0;
       pk.dist_fac    = plist.containsElementNamed("dist_fac")  ? as<double>(plist["dist_fac"]) : 2.0;
       pk.sigma_min   = plist.containsElementNamed("sigma_min") ? as<double>(plist["sigma_min"]) : 0.0005;
       pk.sigma_max   = plist.containsElementNamed("sigma_max") ? as<double>(plist["sigma_max"]) : 0.005;
       pk.RatioPN     = plist.containsElementNamed("ratioSN")   ? as<double>(plist["ratioSN"]) : RATIOPN;
       pk.spcv        = plist.containsElementNamed("spcv")      ? as<double>(plist["spcv"]) : 0.02;
       pk.d2cv        = plist.containsElementNamed("d2cv")      ? as<double>(plist["d2cv"]) : 0.1*pk.spcv;
       pk.d1filt      = plist.containsElementNamed("d1filt")    ? as<int>(plist["d1filt"]) : 0;
       pk.d2filt      = plist.containsElementNamed("d2filt")    ? as<int>(plist["d2filt"]) : 1;

       blocks.SCMIN   = plist.containsElementNamed("scmin")     ? as<double>(plist["scmin"]) : 2;
       _AJLB_         = plist.containsElementNamed("ajlb")      ? as<int>(plist["ajlb"]) : 0;
       _DEBUG_        = plist.containsElementNamed("debug")     ? as<int>(plist["debug"]) : 0;
       _PPM1_         = plist.containsElementNamed("ppm1")      ? as<int>(plist["ppm1"]) : 0;
       _PPM2_         = plist.containsElementNamed("ppm2")      ? as<int>(plist["ppm2"]) : 0;

       // ------- Parameters -------------------------------
       ret["params"] = List::create(_["d2spmeth"] = pk.d2spmeth,
                                    _["optim"] = pk.optim,
                                    _["oa"] = pk.optim_int,
                                    _["os"] = pk.optim_sigma,
                                    _["op"] = pk.optim_ppm,
                                    _["ratioSN"] = pk.RatioPN,
                                    _["spcv"] = pk.spcv,
                                    _["d2cv"] = pk.d2cv,
                                    _["d1filt"] = pk.d1filt,
                                    _["dist_fac"] = pk.dist_fac,
                                    _["sigma_min"] = pk.sigma_min,
                                    _["sigma_max"] = pk.sigma_max,
                                    _["scmin"] = blocks.SCMIN,
                                    _["wmin"] = pk.wmin,
                                    _["wmax"] = pk.wmax );

       // ------- Peaks detection --------------------------
       if(_verbose_>0) Rprintf("Peaks detection\n");
       sp.V = v2;
       find_peaks(&sp,&pk);
       if(_verbose_>0) Rprintf("\tNb detected peaks = %d\n",pk.npic);
       if(_verbose_>0) Rprintf("Peaks selection/ajustment\n");
       //sp.V = v1;
       select_peaks(&sp,&pk);
       if(_verbose_>0) Rprintf("\tNb selected peaks = %d\n",pk.npic);

       // ------- Estimation of Amplitudes & Sigmas --------
       if(_verbose_>0) Rprintf("Sigmas Estimation\n");
       sp.V = v1;
       estime_sigma(&sp,&pk);
       if(_verbose_>0) Rprintf("\tSigma Moy = %f\n",pk.sigma_moy*sp.delta_ppm);

       // ------- Optimisation of Amplitudes & Sigmas ------
       if (pk.optim) {
          if (_verbose_>0) {
             Rprintf("Peaks optimisation (Amplitudes");
             if (pk.optim_sigma) Rprintf(",Sigmas");
             if (pk.optim_ppm)   Rprintf(",ppm");
             Rprintf(")\n");
          }
          sp.V = v1;
          optim_peaks(&sp,&pk,&blocks);

       // ------- Estimation of Amplitudes -----------------
       } else {
          if(_verbose_>0) Rprintf("Amplitudes Estimation\n");
          if (pk.optim_int) { sp.V = v2; estime_AK(&sp,&pk); }
          sp.V = v1;
          optim_peaks(&sp,&pk,&blocks);
       }

       if(_verbose_>0) Rprintf("Peaks selection/ajustment\n");
       select_peaks(&sp,&pk);
       if(_verbose_>0) Rprintf("\tNb selected peaks = %d\n",pk.npic);

       ret["nbpeak"]= pk.npic;

       free_vector(v1);
       free_vector(v2);

       // ------- PeakList ----------------------------------
       double sigfac=1;
       NumericMatrix P(pk.npic, 6);
       for (k=0; k<pk.npic; k++) {
          ppm = pk.optim_ppm ? pk.ppm[k] : ppmval(&sp,pk.pics[k]+pk.pfac[k]);
          P(k,0) = pk.pics[k];
          P(k,1) = ppm;
          P(k,2) = pk.AK[k];
          P(k,3) = sigfac*pk.sigma[k]*sp.delta_ppm;
          P(k,4) = pk.pfac[k];
          P(k,5) = M_PI*pk.AK[k]*pk.sigma[k];
       }
       ret["peaks"] = DataFrame::create( Named("pos") = P(_,0),
                                         Named("ppm") = P(_,1),
                                         Named("amp") = P(_,2),
                                         Named("sigma") = P(_,3),
                                         Named("pfac") = P(_,4),
                                         Named("integral") = P(_,5) );

       // ------- Massifs  ---------------------------------
       NumericMatrix M(blocks.nbblocks, 7);
       for (k=0; k<blocks.nbblocks; k++) {
          M(k,0) = blocks.nstart[k];
          M(k,1) = blocks.nstop[k];
          M(k,2) = ppmval(&sp,blocks.nstart[k]);
          M(k,3) = ppmval(&sp,blocks.nstop[k]);
          M(k,4) = blocks.np[k];
          M(k,5) = blocks.offset[k];
          M(k,6) = blocks.slope[k];
       }
       ret["blocks"] = DataFrame::create( Named("pos1") = M(_,0),
                                          Named("pos2") = M(_,1),
                                          Named("ppm1") = M(_,2),
                                          Named("ppm2") = M(_,3),
                                          Named("nbpeaks") = M(_,4),
                                          Named("offset") = M(_,5),
                                          Named("slope") = M(_,6) );

       // ------- Y model ----------------------------------
    // Note: Index translation  from range[1 - N] to range[0 - N-1]
       NumericVector Ymodel(Y.size());
       for (i=0; i<sp.count_max; i++) {
            Ymodel[i]=0;
            ppm = ppmval(&sp,i+1);
            if (ppm>pk.wmin && ppm<pk.wmax) {
               for (k=0;k<pk.npic;k++)
                    if (pk.AK[k]>0.0) Ymodel[i] += pk.optim_ppm ?
                          pk.AK[k]*lorentz(ppm, pk.ppm[k], sigfac*pk.sigma[k]*sp.delta_ppm) :
                          pk.AK[k]*lorentz(i, pk.pics[k]+pk.pfac[k]-1, sigfac*pk.sigma[k]);
            }
       }
       ret["model"]=Ymodel;

    }

    if(_verbose_>0) Rcerr << "----------------------------------------------------" << std::endl;

    return(ret);
}

void _Ajust_LB_ (SEXP s, SEXP b, int n1, int n2)
{
    NumericVector specR(s), lb(b);
    int k,ni;
    double  a,diff,diff_max,lb_line;

    a=(lb[n2]-lb[n1])/(n2-n1);
    diff_max=0.0; ni=n1;
    for (k=n1; k<n2; k++) {
        lb_line = a*(k-n1)+lb[n1];
        diff = specR[k]< lb_line ? lb_line - specR[k] : 0.0 ;
        if (diff>diff_max) { diff_max=diff; ni=k; }
    }
    if (ni>n1 && ni<n2) {
        lb[ni]=specR[ni];
        _Ajust_LB_(specR,lb,n1,ni);
        _Ajust_LB_(specR,lb,ni,n2);
    }
    else
        for (k=n1; k<n2; k++)
            lb[k]=a*(k-n1)+lb[n1];
}

// [[Rcpp::export]]
SEXP C_MyFuncTest2(SEXP spec, int n1, int n2)
{
    NumericVector Y(spec);
    NumericVector lb(Y.size());
    lb[n1]=Y[n1];
    lb[n2]=Y[n2];
    _Ajust_LB_(Y, lb, n1, n2);
    return(lb);
}
