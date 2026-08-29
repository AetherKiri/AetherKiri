//---------------------------------------------------------------------------
/*
        Risa [りさ]		 alias 吉里吉里3 [kirikiri-3]
         stands for "Risa Is a Stagecraft Architecture"
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
//! @file
//! @brief 実数離散フーリエ変換
//---------------------------------------------------------------------------
#ifndef REALFFT_H
#define REALFFT_H

/*
        Based on
                Real Discrete FFT package from
                        http://momonga.t.u-tokyo.ac.jp/~ooura/fft-j.html
        and
                Ogg Vorbis Optimization Project
                        http://homepage3.nifty.com/blacksword/
*/

//---------------------------------------------------------------------------

void rdft(int, int, float *, int *, float *);

// The scalar implementation remains the compatibility baseline.  krkrz's
// leaf SIMD implementations are exposed only when the current translation
// unit targets the matching ISA; the runtime caller still checks CPU feature
// bits before selecting them.
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || \
    defined(__x86_64__)
void rdft_sse(int, int, float *, int *, float *);
#elif defined(__ARM_NEON) || defined(__aarch64__)
void rdft_neon(int, int, float *, int *, float *);
#endif

//---------------------------------------------------------------------------

#endif
