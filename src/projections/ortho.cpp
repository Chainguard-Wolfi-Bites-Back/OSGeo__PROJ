#define PJ_LIB__
#include <errno.h>
#include "proj.h"
#include "proj_internal.h"
#include <math.h>

PROJ_HEAD(ortho, "Orthographic") "\n\tAzi, Sph&Ell";

namespace { // anonymous namespace
enum Mode {
    N_POLE = 0,
    S_POLE = 1,
    EQUIT  = 2,
    OBLIQ  = 3
};
} // anonymous namespace

namespace { // anonymous namespace
struct pj_opaque {
    double  sinph0;
    double  cosph0;
    double  nu0;
    enum Mode mode;
};
} // anonymous namespace

#define EPS10 1.e-10

static PJ_XY forward_error(PJ *P, PJ_LP lp, PJ_XY xy) {
    proj_errno_set(P, PJD_ERR_TOLERANCE_CONDITION);
    proj_log_trace(P, "Coordinate (%.3f, %.3f) is on the unprojected hemisphere",
                   proj_todeg(lp.lam), proj_todeg(lp.phi));
    return xy;
}

static PJ_XY ortho_s_forward (PJ_LP lp, PJ *P) {           /* Spheroidal, forward */
    PJ_XY xy;
    struct pj_opaque *Q = static_cast<struct pj_opaque*>(P->opaque);
    double  coslam, cosphi, sinphi;

    xy.x = HUGE_VAL; xy.y = HUGE_VAL;

    cosphi = cos(lp.phi);
    coslam = cos(lp.lam);
    switch (Q->mode) {
    case EQUIT:
        if (cosphi * coslam < - EPS10)
            return forward_error(P, lp, xy);
        xy.y = sin(lp.phi);
        break;
    case OBLIQ:
        sinphi = sin(lp.phi);
        if (Q->sinph0 * sinphi + Q->cosph0 * cosphi * coslam < - EPS10)
            return forward_error(P, lp, xy);
        xy.y = Q->cosph0 * sinphi - Q->sinph0 * cosphi * coslam;
        break;
    case N_POLE:
        coslam = - coslam;
                /*-fallthrough*/
    case S_POLE:
        if (fabs(lp.phi - P->phi0) - EPS10 > M_HALFPI)
            return forward_error(P, lp, xy);
        xy.y = cosphi * coslam;
        break;
    }
    xy.x = cosphi * sin(lp.lam);
    return xy;
}


static PJ_LP ortho_s_inverse (PJ_XY xy, PJ *P) {           /* Spheroidal, inverse */
    PJ_LP lp;
    struct pj_opaque *Q = static_cast<struct pj_opaque*>(P->opaque);
    double sinc;

    lp.lam = HUGE_VAL; lp.phi = HUGE_VAL;

    const double rh = hypot(xy.x, xy.y);
    sinc = rh;
    if (sinc > 1.) {
        if ((sinc - 1.) > EPS10) {
            proj_errno_set(P, PJD_ERR_TOLERANCE_CONDITION);
            proj_log_trace(P, "Point (%.3f, %.3f) is outside the projection boundary");
            return lp;
        }
        sinc = 1.;
    }
    const double cosc = sqrt(1. - sinc * sinc); /* in this range OK */
    if (fabs(rh) <= EPS10) {
        lp.phi = P->phi0;
        lp.lam = 0.0;
    } else {
        switch (Q->mode) {
        case N_POLE:
            xy.y = -xy.y;
            lp.phi = acos(sinc);
            break;
        case S_POLE:
            lp.phi = - acos(sinc);
            break;
        case EQUIT:
            lp.phi = xy.y * sinc / rh;
            xy.x *= sinc;
            xy.y = cosc * rh;
            goto sinchk;
        case OBLIQ:
            lp.phi = cosc * Q->sinph0 + xy.y * sinc * Q->cosph0 /rh;
            xy.y = (cosc - Q->sinph0 * lp.phi) * rh;
            xy.x *= sinc * Q->cosph0;
        sinchk:
            if (fabs(lp.phi) >= 1.)
                lp.phi = lp.phi < 0. ? -M_HALFPI : M_HALFPI;
            else
                lp.phi = asin(lp.phi);
            break;
        }
        lp.lam = (xy.y == 0. && (Q->mode == OBLIQ || Q->mode == EQUIT))
             ? (xy.x == 0. ? 0. : xy.x < 0. ? -M_HALFPI : M_HALFPI)
                           : atan2(xy.x, xy.y);
    }
    return lp;
}


static PJ_XY ortho_e_forward (PJ_LP lp, PJ *P) {           /* Ellipsoidal, forward */
    PJ_XY xy;
    struct pj_opaque *Q = static_cast<struct pj_opaque*>(P->opaque);

    // From EPSG guidance note 7.2
    const double cosphi = cos(lp.phi);
    const double sinphi = sin(lp.phi);
    const double coslam = cos(lp.lam);
    const double sinlam = sin(lp.lam);
    const double nu = 1.0 / sqrt(1.0 - P->es * sinphi * sinphi);
    xy.x = nu * cosphi * sinlam;
    xy.y = nu * (sinphi * Q->cosph0 - cosphi * Q->sinph0 * coslam) +
            P->es * (Q->nu0 * Q->sinph0 - nu * sinphi) * Q->cosph0;

    return xy;
}



static PJ_LP ortho_e_inverse (PJ_XY xy, PJ *P) {           /* Ellipsoidal, inverse */
    PJ_LP lp;
    struct pj_opaque *Q = static_cast<struct pj_opaque*>(P->opaque);

    // From EPSG guidance note 7.2

    // It suggests as initial guess:
    // lp.lam = 0;
    // lp.phi = P->phi0;
    // But for poles, this will not converge well. Better use:
    lp = ortho_s_inverse(xy, P);

    for( int i = 0; i < 20; i++ )
    {
        const double cosphi = cos(lp.phi);
        const double sinphi = sin(lp.phi);
        const double coslam = cos(lp.lam);
        const double sinlam = sin(lp.lam);
        const double one_minus_es_sinphi2 = 1.0 - P->es * sinphi * sinphi;
        const double nu = 1.0 / sqrt(one_minus_es_sinphi2);
        PJ_XY xy_new;
        xy_new.x = nu * cosphi * sinlam;
        xy_new.y = nu * (sinphi * Q->cosph0 - cosphi * Q->sinph0 * coslam) +
                P->es * (Q->nu0 * Q->sinph0 - nu * sinphi) * Q->cosph0;
        const double rho = (1.0 - P->es) * nu / one_minus_es_sinphi2;
        const double J11 = -rho * sinphi * sinlam;
        const double J12 = nu * cosphi * coslam;
        const double J21 = rho * (cosphi * Q->cosph0 + sinphi * Q->sinph0 * coslam);
        const double J22 = nu * Q->sinph0 * Q->cosph0 * sinlam;
        const double D = J11 * J22 - J12 * J21;
        const double dx = xy.x - xy_new.x;
        const double dy = xy.y - xy_new.y;
        const double dphi = (J22 * dx - J12 * dy) / D;
        const double dlam = (-J21 * dx + J11 * dy) / D;
        lp.phi += dphi;
        if( lp.phi > M_PI_2) lp.phi = M_PI_2;
        else if( lp.phi < -M_PI_2) lp.phi = -M_PI_2;
        lp.lam += dlam;
        if( fabs(dphi) < 1e-12 && fabs(dlam) < 1e-12 )
        {
            return lp;
        }
    }
    pj_ctx_set_errno(P->ctx, PJD_ERR_NON_CONVERGENT);
    return lp;
}


PJ *PROJECTION(ortho) {
    struct pj_opaque *Q = static_cast<struct pj_opaque*>(pj_calloc (1, sizeof (struct pj_opaque)));
    if (nullptr==Q)
        return pj_default_destructor(P, ENOMEM);
    P->opaque = Q;

    Q->sinph0 = sin(P->phi0);
    Q->cosph0 = cos(P->phi0);
    if (fabs(fabs(P->phi0) - M_HALFPI) <= EPS10)
        Q->mode = P->phi0 < 0. ? S_POLE : N_POLE;
    else if (fabs(P->phi0) > EPS10) {
        Q->mode = OBLIQ;
    } else
        Q->mode = EQUIT;
    if( P->es == 0 )
    {
        P->inv = ortho_s_inverse;
        P->fwd = ortho_s_forward;
    }
    else
    {
        Q->nu0 = 1.0 / sqrt(1.0 - P->es * Q->sinph0 * Q->sinph0);
        P->inv = ortho_e_inverse;
        P->fwd = ortho_e_forward;
    }

    return P;
}

