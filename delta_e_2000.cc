#include <math.h>

#include "pixel.h"

#include <iostream>

double de00(const dblpixel Labstd, const dblpixel Labsample, bool squared) {
	// Compute the CIEDE2000 color-difference between the sample between
	// a reference with CIELab coordinates Labsample and a standard with
	// CIELab coordinates.

	// Translated from the Matlab code accompanying the article
	// "The CIEDE2000 Color-Difference Formula: Implementation Notes,
	// Supplementary Test Data, and Mathematical Observations,", G. Sharma,
	// W. Wu, E. N. Dalal, Color Research and Application, vol. 30. No. 1, pp.
	// 21-30, February 2005.
	// available at http://www.ece.rochester.edu/~/gsharma/ciede2000/

	double kl = 1, kc = 1, kh = 1;

	double Lstd = Labstd.r;
	double astd = Labstd.g;
	double bstd = Labstd.b;

	double Cabstd = sqrt(astd*astd+bstd*bstd);

	double Lsample = Labsample.r;
	double asample = Labsample.g;
	double bsample = Labsample.b;

	double Cabsample = sqrt(asample*asample+bsample*bsample);
	double Cabarithmean = (Cabstd + Cabsample)/2;

	double G = 0.5 * (1 - sqrt( pow(Cabarithmean, 7) / (pow(Cabarithmean, 7) +
		pow(25, 7))));

	double apstd = (1+G) * astd; // aprime in paper
	double apsample = (1+G) *asample; // aprime in paper
	double Cpsample = sqrt(apsample*apsample +bsample*bsample);

	double Cpstd = sqrt(apstd*apstd+bstd*bstd);

	// Compute product of chromas and locations at which it is zero for
	// use later
	double Cpprod = (Cpsample *Cpstd);

	double hpstd, hpsample, dL, dC;

	// Ensure hue is between 0 and 2pi
	if (bstd == 0 && apstd == 0) {
		hpstd = 0;
	} else {
		hpstd = atan2(bstd, apstd);
	}

	while (hpstd < 0) {
		hpstd += 2 * M_PI; // Rollover negative ones
	}

	if (fabs(apstd) + fabs(bstd) == 0) {
		hpstd = 0;
	}

	// Same for hpsample
	if (bsample == 0 && apsample == 0) {
		hpsample = 0;
	} else {
		hpsample = atan2(bsample, apsample);
	}

	while (hpsample < 0) {
		hpsample += 2 * M_PI;
	}

	if (fabs(apsample) + fabs(bsample) == 0) {
		hpsample = 0;
	}

	dL = (Lsample-Lstd);
	dC = (Cpsample-Cpstd);

	// Computation of hue difference
	double dhp;
	if (Cpprod == 0) {
		// set chroma difference to zero if the product of chromas is zero
		dhp = 0;
	} else {
		dhp = (hpsample-hpstd);
		dhp = dhp - 2* M_PI * (dhp > M_PI);
		dhp = dhp + 2 *M_PI * (dhp < -M_PI);
	}

	// Note that the defining equations actually need
	// signed Hue and chroma differences which is different
	// from prior color difference formulae

	double dH = 2*sqrt(Cpprod) *sin(dhp/2);
	double dH2 = 4*Cpprod*pow(sin(dhp/2), 2);

	//weighting functions
	double Lp = (Lsample+Lstd)/2;
	double Cp = (Cpstd+Cpsample)/2;

	// Average Hue Computation
	// This is equivalent to that in the paper but simpler programmatically.
	// Note average hue is computed in radians and converted to degrees only
	// where needed
	double hp = (hpstd+hpsample)/2;

	// Identify positions for which abs hue diff exceeds 180 degrees
	if (fabs(hpstd - hpsample) > M_PI) {
		hp -= M_PI;
	}

	// rollover ones that come -ve
	while (hp < 0) { hp += 2 * M_PI; }

	// Check if one of the chroma values is zero, in which case set
	// mean hue to the sum which is equivalent to other value
	if (Cpprod == 0) {
		hp = hpsample + hpstd;
	}

	double Lpm502 = (Lp-50) * (Lp-50);
	double Sl = 1 + 0.015* Lpm502/sqrt(20+Lpm502);
	double Sc = 1+0.045*Cp;
	double T = 1 - 0.17*cos(hp - M_PI/6 ) + 0.24*cos(2*hp)
		+ 0.32*cos(3*hp+M_PI/30) - 0.20*cos(4*hp-63*M_PI/180);

    double Sh = 1 + 0.015*Cp*T;
    double delthetarad = (30*M_PI/180)*exp(-(pow((180/M_PI*hp-275)/25, 2)));
	double Rc = 2*sqrt(pow(Cp, 7)/(pow(Cp,7) + pow(25,7)));
	double RT = -sin(2*delthetarad)*Rc;

	double klSl = kl*Sl;
	double kcSc = kc*Sc;
	double khSh = kh*Sh;

	// The CIE 00 color difference
	double de_squared = ( pow(dL/klSl, 2) + pow(dC/kcSc, 2) + pow(dH/khSh, 2) + RT*(dC/kcSc)*(dH/khSh) );

	if (squared) {
		return de_squared;
	} else {
		return sqrt(de_squared);
	}
}

bool test_one_de00(double aL, double aa, double ab,
	double bL, double ba, double bb, double expected, double precision) {

	dblpixel a, b;
	a.r = aL;
	a.g = aa;
	a.b = ab;
	b.r = bL;
	b.g = ba;
	b.b = bb;

	double observed = de00(a, b, false);

	return (fabs(observed-expected) < pow(10, -precision));

}

bool test_de00() {
	// Some test points from
	// http://www2.ece.rochester.edu/~gsharma/ciede2000/dataNprograms/ciede2000testdata.txt
	return
		test_one_de00(50,  2.6772, -79.7751, 50,  0, -82.7485, 2.0425, 4) &&
		test_one_de00(50,  3.1571, -77.2803, 50,  0, -82.7485, 2.8615, 4) &&
		test_one_de00(50,  2.8361, -74.0200, 50,  0, -82.7485, 3.4412, 4) &&
		test_one_de00(50, -1.3802, -84.2814, 50,  0, -82.7485, 1.0000, 4) &&
		test_one_de00(50, -1.1848, -84.8006, 50,  0, -82.7485, 1.0000, 4) &&
		test_one_de00(50, -0.9009, -85.5211, 50,  0, -82.7485, 1.0000, 4) &&
		test_one_de00(50,  0.0000,   0.0000, 50, -1,   2.0000, 2.3669, 4) &&
		test_one_de00(50, -1.0000,   2.0000, 50,  0,   0.0000, 2.3669, 4);
}