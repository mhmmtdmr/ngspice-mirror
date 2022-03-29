/*
**** 
* Alessio Cacciatori 2021
****
*/
#include "ngspice/ngspice.h"
#include "ngspice/cktdefs.h"
#include "ngspice/spardefs.h"
#include "ngspice/devdefs.h"
#include "ngspice/sperror.h"

#ifdef XSPICE
#include "ngspice/evt.h"
#include "ngspice/enh.h"
/* gtri - add - wbk - 12/19/90 - Add headers */ 
#include "ngspice/mif.h"
#include "ngspice/evtproto.h"
#include "ngspice/ipctiein.h"
/* gtri - end - wbk */
#endif

#define SQR(x) ((x) * (x))


#ifdef RFSPICE
#include "vsrc/vsrcdefs.h"
#include "vsrc/vsrcext.h"
#include "../maths/dense/dense.h"
#include "../maths/dense/denseinlines.h"



#define INIT_STATS() \
do { \
    startTime  = SPfrontEnd->IFseconds();       \
    startdTime = ckt->CKTstat->STATdecompTime;  \
    startsTime = ckt->CKTstat->STATsolveTime;   \
    startlTime = ckt->CKTstat->STATloadTime;    \
    startkTime = ckt->CKTstat->STATsyncTime;    \
} while(0)

#define UPDATE_STATS(analysis) \
do { \
    ckt->CKTcurrentAnalysis = analysis; \
    ckt->CKTstat->STATacTime += SPfrontEnd->IFseconds() - startTime; \
    ckt->CKTstat->STATacDecompTime += ckt->CKTstat->STATdecompTime - startdTime; \
    ckt->CKTstat->STATacSolveTime += ckt->CKTstat->STATsolveTime - startsTime; \
    ckt->CKTstat->STATacLoadTime += ckt->CKTstat->STATloadTime - startlTime; \
    ckt->CKTstat->STATacSyncTime += ckt->CKTstat->STATsyncTime - startkTime; \
} while(0)

/*----------------------------------
* Auxiliary data for S-Y-Z matrix
* conversion
*-----------------------------------
*/
CMat* eyem = NULL;
CMat* zref = NULL;
CMat* gn = NULL;
CMat* gninv = NULL;
CMat* vNoise = NULL;
CMat* iNoise = NULL;
// Aux data for Noise Calculation
double NF = 0;
double Rn = 0;
cplx   Sopt;
double Fmin = 0;
double refPortY0;

int
CKTspnoise(CKTcircuit * ckt, int mode, int operation, Ndata * data)
{
    NOISEAN* job = (NOISEAN*)ckt->CKTcurJob;

    double outNdens;
    int i;
#ifdef LEGACY
    IFvalue outData;    /* output variable (points to list of outputs)*/
    IFvalue refVal; /* reference variable (always 0)*/
#endif
    int error;
    outNdens = 0.0;
    job->NStpsSm = 1;

    /* let each device decide how many and what type of noise sources it has */

    for (i = 0; i < DEVmaxnum; i++) {
        if (DEVices[i] && DEVices[i]->DEVnoise && ckt->CKThead[i]) {
            int a = 0;
            a++;
            if (a == 0) a = 2;
            error = DEVices[i]->DEVnoise(mode, operation, ckt->CKThead[i],
                ckt, data, &outNdens);
            if (error) return (error);
        }
    }

    switch (operation) {

    case N_OPEN:
        // Init all matrices
        cinit(ckt->CKTNoiseCYmat, 0.0, 0.0);
        cinit(ckt->CKTadjointRHS, 0.0, 0.0);
        break;

    case N_CALC:
    {

        // We have the Cy noise matrix,
       
        // Equations from Stephen Maas 'Noise'
        double knorm = 4.0 * CONSTboltz * (ckt->CKTtemp);
        CMat* tempCy = cscalarmultiply(ckt->CKTNoiseCYmat, 1.0/knorm); // cmultiply(, YConj);


        if (ckt->CKTportCount == 2)
        {
            
            double Y21mod = cmodsqr(ckt->CKTYmat->d[1][0]);
            Rn = (tempCy->d[1][1].re / Y21mod) ;
            cplx Ycor = csubco(ckt->CKTYmat->d[0][0],
                cmultco(
                    cdivco(tempCy->d[0][1], tempCy->d[1][1]),
                    tempCy->d[1][0]
                ));
            double Y11_Ycor = cmodsqr(csubco(ckt->CKTYmat->d[0][0], Ycor));

            double Gu = tempCy->d[0][0].re - Rn * Y11_Ycor;

            cplx Ysopt; Ysopt.re = sqrt(SQR(Ycor.re) + Gu / Rn); Ysopt.im = -Ycor.im;
            cplx Y0; Y0.re = refPortY0; Y0.im = 0.0;
            Sopt = cdivco(csubco(Y0, Ysopt),
                caddco(Y0, Ysopt));
            Fmin = 1.0 + 2.0 * Rn * (Ycor.re + Ysopt.re);
            double Ysoptmod = cmodu(csubco(Y0, Ysopt));
            NF = Fmin + (Rn / Ysopt.re) * SQR(Ysoptmod);
            Fmin = 10.0 * log10(Fmin);
            NF = 10.0 * log10(NF);
        }
     
        freecmat(tempCy);
     }


#ifdef LEGACY
        switch (mode) {

        case N_DENS:
            if ((job->NStpsSm == 0)
                || data->prtSummary)
            {
                data->outpVector[data->outNumber++] = outNdens;
                data->outpVector[data->outNumber++] =
                    (outNdens * data->GainSqInv);

                refVal.rValue = data->freq; /* the reference is the freq */
                if (!data->squared)
                    for (i = 0; i < data->outNumber; i++)
                        if (data->squared_value[i])
                            data->outpVector[i] = sqrt(data->outpVector[i]);
                outData.v.numValue = data->outNumber; /* vector number */
                outData.v.vec.rVec = data->outpVector; /* vector of outputs */
                SPfrontEnd->OUTpData(data->NplotPtr, &refVal, &outData);
            }
            break;

        case INT_NOIZ:
            data->outpVector[data->outNumber++] = data->outNoiz;
            data->outpVector[data->outNumber++] = data->inNoise;
            if (!data->squared)
                for (i = 0; i < data->outNumber; i++)
                    if (data->squared_value[i])
                        data->outpVector[i] = sqrt(data->outpVector[i]);
            outData.v.vec.rVec = data->outpVector; /* vector of outputs */
            outData.v.numValue = data->outNumber; /* vector number */
            SPfrontEnd->OUTpData(data->NplotPtr, &refVal, &outData);
            break;

        default:
            return (E_INTERN);
        }
#endif
        break;

    case N_CLOSE:
        SPfrontEnd->OUTendPlot(data->NplotPtr);
        FREE(data->namelist);
        FREE(data->outpVector);
        FREE(data->squared_value);
        freecmat(ckt->CKTNoiseCYmat);
        freecmat(ckt->CKTadjointRHS);
        ckt->CKTNoiseCYmat = NULL;
        ckt->CKTadjointRHS = NULL;
        break;

    default:
        return (E_INTERN);
    }
    return (OK);
}


int
NInspIter(CKTcircuit * ckt, VSRCinstance* port)
{
    int i;

    /* clear out the right hand side vector */

    for (i = 0; i <= SMPmatSize(ckt->CKTmatrix); i++) {
        ckt->CKTrhs[i] = 0.0;
        ckt->CKTirhs[i] = 0.0;
    }

    ckt->CKTrhs[port->VSRCposNode] = 1.0;     /* apply unit current excitation */
    ckt->CKTrhs[port->VSRCnegNode] = -1.0;
    SMPcaSolve(ckt->CKTmatrix, ckt->CKTrhs, ckt->CKTirhs, ckt->CKTrhsSpare,
        ckt->CKTirhsSpare);

    ckt->CKTrhs[0] = 0.0;
    ckt->CKTirhs[0] = 0.0;

    return (OK);
}

int initSPmatrix(CKTcircuit* ckt, int doNoise)
{

    if (ckt->CKTAmat != NULL) freecmat(ckt->CKTAmat);
    if (ckt->CKTBmat != NULL) freecmat(ckt->CKTBmat);
    if (ckt->CKTSmat != NULL) freecmat(ckt->CKTSmat);
    if (ckt->CKTYmat != NULL) freecmat(ckt->CKTYmat);
    if (ckt->CKTZmat != NULL) freecmat(ckt->CKTZmat);
    if (eyem != NULL)          freecmat(eyem);
    if (zref != NULL)         freecmat(zref);
    if (gn != NULL)           freecmat(gn);
    if (gninv != NULL)          freecmat(gninv);


    ckt->CKTAmat = newcmat(ckt->CKTportCount, ckt->CKTportCount, 0.0, 0.0);
    if (ckt->CKTAmat == NULL)
        return (E_NOMEM);
    ckt->CKTBmat = newcmat(ckt->CKTportCount, ckt->CKTportCount, 0.0, 0.0);
    if (ckt->CKTBmat == NULL)
        return (3);

    ckt->CKTSmat = newcmat(ckt->CKTportCount, ckt->CKTportCount, 0.0, 0.0);
    if (ckt->CKTSmat == NULL)
        return (E_NOMEM);

    ckt->CKTYmat = newcmat(ckt->CKTportCount, ckt->CKTportCount, 0.0, 0.0);
    if (ckt->CKTYmat == NULL)
        return (E_NOMEM);

    ckt->CKTZmat = newcmat(ckt->CKTportCount, ckt->CKTportCount, 0.0, 0.0);
    if (ckt->CKTZmat == NULL)
        return (E_NOMEM);

    eyem = ceye(ckt->CKTportCount);
    if (eyem == NULL)
        return (E_NOMEM);

    zref = newcmat(ckt->CKTportCount, ckt->CKTportCount, 0.0, 0.0);
    if (zref == NULL)
        return (E_NOMEM);

    gn = newcmat(ckt->CKTportCount, ckt->CKTportCount, 0.0, 0.0);
    if (gn == NULL)
        return (E_NOMEM);

    gninv = newcmat(ckt->CKTportCount, ckt->CKTportCount, 0.0, 0.0);
    if (gninv == NULL)
        return (E_NOMEM);

    // Now that we have found the model, we may init the Zref and Gn ports
    if (ckt->CKTVSRCid>=0)
        VSRCspinit(ckt->CKThead[ckt->CKTVSRCid], ckt, zref, gn, gninv);

    if (doNoise)
    {

        // Allocate matrices and vector
        if (ckt->CKTNoiseCYmat != NULL) freecmat(ckt->CKTNoiseCYmat);
        ckt->CKTNoiseCYmat = newcmatnoinit(ckt->CKTportCount, ckt->CKTportCount);
        if (ckt->CKTNoiseCYmat == NULL) return (E_NOMEM);

        // Use CKTadjointRHS as a convenience storage for all solutions (each solution per each 
        // port excitation)
        if (ckt->CKTadjointRHS != NULL) freecmat(ckt->CKTadjointRHS);
        ckt->CKTadjointRHS = newcmatnoinit(ckt->CKTportCount, ckt->CKTmaxEqNum);
        if (ckt->CKTadjointRHS == NULL) return (E_NOMEM);

        if (vNoise != NULL) freecmat(vNoise);
        if (iNoise != NULL) freecmat(iNoise);

        vNoise = newcmatnoinit(1, ckt->CKTportCount);
        iNoise = newcmatnoinit(1, ckt->CKTportCount);

        VSRCinstance* refPort = (VSRCinstance*)(ckt->CKTrfPorts[0]);
        refPortY0 = refPort->VSRCportY0;

    }
    return (OK);
}

void deleteSPmatrix(CKTcircuit* ckt)
{
    if (ckt->CKTAmat != NULL) freecmat(ckt->CKTAmat);
    if (ckt->CKTBmat != NULL) freecmat(ckt->CKTBmat);
    if (ckt->CKTSmat != NULL) freecmat(ckt->CKTSmat);
    if (ckt->CKTYmat != NULL) freecmat(ckt->CKTYmat);
    if (ckt->CKTZmat != NULL) freecmat(ckt->CKTZmat);
    if (eyem != NULL)          freecmat(eyem);
    if (zref != NULL)         freecmat(zref);
    if (gn != NULL)           freecmat(gn);
    if (gninv != NULL)          freecmat(gninv);
    eyem = NULL;
    zref = NULL;
    gn = NULL;
    gninv = NULL;

    ckt->CKTAmat = NULL;
    ckt->CKTBmat = NULL;
    ckt->CKTSmat = NULL;
    ckt->CKTZmat = NULL;
    ckt->CKTYmat = NULL;

    if (ckt->CKTNoiseCYmat != NULL) freecmat(ckt->CKTNoiseCYmat);
    if (ckt->CKTadjointRHS != NULL) freecmat(ckt->CKTadjointRHS);
    if (vNoise != NULL) freecmat(vNoise);
    if (iNoise != NULL) freecmat(iNoise);


    vNoise = NULL;
    iNoise = NULL;
    ckt->CKTNoiseCYmat = NULL;
    ckt->CKTadjointRHS = NULL;
}

int
SPan(CKTcircuit *ckt, int restart)
{


    SPAN *job = (SPAN *) ckt->CKTcurJob;

    double freq;
    double freqTol; /* tolerence parameter for finding final frequency */
    double startdTime;
    double startsTime;
    double startlTime;
    double startkTime;
    double startTime;
    int error;
    int numNames;
    int i;
    IFuid *nameList;  /* va: tmalloc'ed list of names */
    IFuid freqUid;
    static runDesc *spPlot = NULL;
    runDesc *plot = NULL;

    double* rhswoPorts = NULL;
    double* irhswoPorts = NULL;

    /* variable must be static, for continuation of interrupted (Ctrl-C),
    longer lasting noise anlysis */
    static Ndata* data=NULL;
    if (job->SPdoNoise)
    {
        data = TMALLOC(Ndata, 1);
    }


    if (ckt->CKTportCount == 0)
    {
        fprintf(stderr, "No RF Port is present\n");
        return (E_PARMVAL);
    }



#ifdef XSPICE
/* gtri - add - wbk - 12/19/90 - Add IPC stuff and anal_init and anal_type */

    /* Tell the beginPlot routine what mode we're in */

    // For now, let's keep this as IPC_ANAL_AC (TBD)
    g_ipc.anal_type = IPC_ANAL_AC;

    /* Tell the code models what mode we're in */
    g_mif_info.circuit.anal_type = MIF_DC;
    g_mif_info.circuit.anal_init = MIF_TRUE;

/* gtri - end - wbk */
#endif

    /* start at beginning */
    if (job->SPsaveFreq == 0 || restart) {
        if (job->SPnumberSteps < 1)
            job->SPnumberSteps = 1;

        switch (job->SPstepType) {

        case DECADE:
            if (job->SPstartFreq <= 0) {
                fprintf(stderr, "ERROR: AC startfreq <= 0\n");
                return E_PARMVAL;
            }
            job->SPfreqDelta =
                exp(log(10.0) / job->SPnumberSteps);
            break;
        case OCTAVE:
            if (job->SPstartFreq <= 0) {
                fprintf(stderr, "ERROR: AC startfreq <= 0\n");
                return E_PARMVAL;
            }
            job->SPfreqDelta =
                exp(log(2.0) / job->SPnumberSteps);
            break;
        case LINEAR:
            if (job->SPnumberSteps - 1 > 1)
                job->SPfreqDelta =
                (job->SPstopFreq -
                    job->SPstartFreq) /
                (job->SPnumberSteps - 1);
            else
                /* Patch from: Richard McRoberts
                * This patch is for a rather pathological case:
                * a linear step with only one point */
                job->SPfreqDelta = 0;
            break;
        default:
            return(E_BADPARM);
        }

        if (job->SPdoNoise)
        {
            data->lstFreq = job->SPstartFreq - 1;
            data->delFreq = 1.0;
        }

#ifdef XSPICE
        /* gtri - begin - wbk - Call EVTop if event-driven instances exist */

        if (ckt->evt->counts.num_insts != 0) {
            error = EVTop(ckt,
                (ckt->CKTmode & MODEUIC) | MODEDCOP | MODEINITJCT,
                (ckt->CKTmode & MODEUIC) | MODEDCOP | MODEINITFLOAT,
                ckt->CKTdcMaxIter,
                MIF_TRUE);
            EVTdump(ckt, IPC_ANAL_DCOP, 0.0);
            EVTop_save(ckt, MIF_TRUE, 0.0);
        }
        else
#endif 
            /* If no event-driven instances, do what SPICE normally does */
            if (!ckt->CKTnoopac) { /* skip OP if option NOOPAC is set and circuit is linear */
                error = CKTop(ckt,
                    (ckt->CKTmode & MODEUIC) | MODEDCOP | MODEINITJCT,
                    (ckt->CKTmode & MODEUIC) | MODEDCOP | MODEINITFLOAT,
                    ckt->CKTdcMaxIter);

                if (error) {
                    fprintf(stdout, "\nAC operating point failed -\n");
                    CKTncDump(ckt);
                    return(error);
                }
            }
            else
                fprintf(stdout, "\n Linear circuit, option noopac given: no OP analysis\n");

#ifdef XSPICE
        /* gtri - add - wbk - 12/19/90 - Add IPC stuff */

            /* Send the operating point results for Mspice compatibility */
        if (g_ipc.enabled)
        {
            /* Call CKTnames to get names of nodes/branches used by
                BeginPlot */
                /* Probably should free nameList after this block since
                    called again... */
            error = CKTnames(ckt, &numNames, &nameList);
            if (error) return(error);

            /* We have to do a beginPlot here since the data to return is
             * different for the DCOP than it is for the AC analysis.
             * Moreover the begin plot has not even been done yet at this
             * point...
             */
            SPfrontEnd->OUTpBeginPlot(ckt, ckt->CKTcurJob,
                ckt->CKTcurJob->JOBname,
                NULL, IF_REAL,
                numNames, nameList, IF_REAL,
                &spPlot);
            txfree(nameList);

            ipc_send_dcop_prefix();
            CKTdump(ckt, 0.0, spPlot);
            ipc_send_dcop_suffix();

            SPfrontEnd->OUTendPlot(spPlot);
        }
        /* gtri - end - wbk */
#endif

        ckt->CKTmode = (ckt->CKTmode & MODEUIC) | MODEDCOP | MODEINITSMSIG;
        error = CKTload(ckt);
        if (error) return(error);

        error = CKTnames(ckt, &numNames, &nameList);
        if (error) return(error);

        if (ckt->CKTkeepOpInfo) {
            /* Dump operating point. */
            error = SPfrontEnd->OUTpBeginPlot(ckt, ckt->CKTcurJob,
                "AC Operating Point",
                NULL, IF_REAL,
                numNames, nameList, IF_REAL,
                &plot);
            if (error) return(error);
            CKTdump(ckt, 0.0, plot);
            SPfrontEnd->OUTendPlot(plot);
            plot = NULL;
        }

        unsigned int extraSPdataLength = 3 * ckt->CKTportCount * ckt->CKTportCount;
        if (job->SPdoNoise)
        {
            extraSPdataLength += ckt->CKTportCount * ckt->CKTportCount; // Add Cy
            if (ckt->CKTportCount == 2)
                extraSPdataLength += 4;
        }

        nameList = (IFuid*)TREALLOC(IFuid, nameList, numNames + extraSPdataLength);


        // Create UIDs
        for (unsigned int dest = 1; dest <= ckt->CKTportCount; dest++)
            for (unsigned int j = 1; j <= ckt->CKTportCount; j++)
            {
                char tmpBuf[32];
                sprintf(tmpBuf, "S_%d_%d", dest, j);

                SPfrontEnd->IFnewUid(ckt, &(nameList[numNames++]), NULL, tmpBuf, UID_OTHER, NULL);
            }

        // Create UIDs
        for (unsigned int dest = 1; dest <= ckt->CKTportCount; dest++)
            for (unsigned int j = 1; j <= ckt->CKTportCount; j++)
            {
                char tmpBuf[32];
                sprintf(tmpBuf, "Y_%d_%d", dest, j);

                SPfrontEnd->IFnewUid(ckt, &(nameList[numNames++]), NULL, tmpBuf, UID_OTHER, NULL);
            }

        // Create UIDs
        for (unsigned int dest = 1; dest <= ckt->CKTportCount; dest++)
            for (unsigned int j = 1; j <= ckt->CKTportCount; j++)
            {
                char tmpBuf[32];
                sprintf(tmpBuf, "Z_%d_%d", dest, j);

                SPfrontEnd->IFnewUid(ckt, &(nameList[numNames++]), NULL, tmpBuf, UID_OTHER, NULL);
            }

        // Add noise related output, if needed
        if (job->SPdoNoise)
        {
            // Create UIDs
            for (unsigned int dest = 1; dest <= ckt->CKTportCount; dest++)
                for (unsigned int j = 1; j <= ckt->CKTportCount; j++)
                {
                    char tmpBuf[32];
                    sprintf(tmpBuf, "Cy_%d_%d", dest, j);

                    SPfrontEnd->IFnewUid(ckt, &(nameList[numNames++]), NULL, tmpBuf, UID_OTHER, NULL);
                }


            // Add NFMin, SOpt, Rn (related to port 1)
            if (ckt->CKTportCount == 2)
            {
                SPfrontEnd->IFnewUid(ckt, &(nameList[numNames++]), NULL, "NF", UID_OTHER, NULL);
                SPfrontEnd->IFnewUid(ckt, &(nameList[numNames++]), NULL, "SOpt", UID_OTHER, NULL);
                SPfrontEnd->IFnewUid(ckt, &(nameList[numNames++]), NULL, "NFmin", UID_OTHER, NULL);
                SPfrontEnd->IFnewUid(ckt, &(nameList[numNames++]), NULL, "Rn", UID_OTHER, NULL);
            }
        }


        SPfrontEnd->IFnewUid(ckt, &freqUid, NULL, "frequency", UID_OTHER, NULL);
        error = SPfrontEnd->OUTpBeginPlot(ckt, ckt->CKTcurJob,
            ckt->CKTcurJob->JOBname,
            freqUid, IF_REAL,
            numNames, nameList, IF_COMPLEX,
            &spPlot);



	tfree(nameList);		
	if(error) return(error);

        if (job->SPstepType != LINEAR) {
	    SPfrontEnd->OUTattributes (spPlot, NULL, OUT_SCALE_LOG, NULL);
	}
        freq = job->SPstartFreq;

    } else {    /* continue previous analysis */
        freq = job->SPsaveFreq;
        job->SPsaveFreq = 0; /* clear the 'old' frequency */
	/* fix resume? saj, indeed !*/
        error = SPfrontEnd->OUTpBeginPlot (NULL, NULL,
                                           NULL,
                                           NULL, 0,
                                           666, NULL, 666,
                                           &spPlot);
	/* saj*/    
    }
        
    switch (job->SPstepType) {
    case DECADE:
    case OCTAVE:
        freqTol = job->SPfreqDelta *
            job->SPstopFreq * ckt->CKTreltol;
        break;
    case LINEAR:
        freqTol = job->SPfreqDelta * ckt->CKTreltol;
        break;
    default:
        return(E_BADPARM);
    }


#ifdef XSPICE
/* gtri - add - wbk - 12/19/90 - Set anal_init and anal_type */

    g_mif_info.circuit.anal_init = MIF_TRUE;

    /* Tell the code models what mode we're in */
    g_mif_info.circuit.anal_type = MIF_AC;

/* gtri - end - wbk */
#endif

    INIT_STATS();

    ckt->CKTcurrentAnalysis = DOING_AC | DOING_SP;

    if (initSPmatrix(ckt, job->SPdoNoise))
        return (E_NOMEM);

    // Create Noise UID, if needed

    if (job->SPdoNoise)
    {

        data->numPlots = 0;                /* we don't have any plots  yet */
        error = CKTspnoise(ckt, N_DENS, N_OPEN, data);
        if (error) return(error);
    }

    ckt->CKTactivePort = 0;
    /* main loop through all scheduled frequencies */
    while (freq <= job->SPstopFreq + freqTol) {

        unsigned int activePort = 0;
        //
        if (SPfrontEnd->IFpauseTest()) {
            /* user asked us to pause via an interrupt */
            job->SPsaveFreq = freq;
            return(E_PAUSE);
        }
        ckt->CKTomega = 2.0 * M_PI * freq;

        /* Update opertating point, if variable 'hertz' is given */
        if (ckt->CKTvarHertz) {
#ifdef XSPICE
            /* Call EVTop if event-driven instances exist */

            if (ckt->evt->counts.num_insts != 0) {
                error = EVTop(ckt,
                    (ckt->CKTmode & MODEUIC) | MODEDCOP | MODEINITJCT,
                    (ckt->CKTmode & MODEUIC) | MODEDCOP | MODEINITFLOAT,
                    ckt->CKTdcMaxIter,
                    MIF_TRUE);
                EVTdump(ckt, IPC_ANAL_DCOP, 0.0);
                EVTop_save(ckt, MIF_TRUE, 0.0);
            }
            else
#endif 
                // If no event-driven instances, do what SPICE normally does
                error = CKTop(ckt,
                    (ckt->CKTmode & MODEUIC) | MODEDCOP | MODEINITJCT,
                    (ckt->CKTmode & MODEUIC) | MODEDCOP | MODEINITFLOAT,
                    ckt->CKTdcMaxIter);

            if (error) {
                fprintf(stdout, "\nAC operating point failed -\n");
                CKTncDump(ckt);
                return(error);
            }
            ckt->CKTmode = (ckt->CKTmode & MODEUIC) | MODEDCOP | MODEINITSMSIG;
            error = CKTload(ckt);
            if (error) {
                tfree(data);  return(error);
            }
        }

        // Store previous rhs 
        if (rhswoPorts == NULL)
            rhswoPorts = (double*)TMALLOC(double, ckt->CKTmaxEqNum);
        else
            rhswoPorts = (double*)TREALLOC(double, rhswoPorts, ckt->CKTmaxEqNum);

        if (rhswoPorts == NULL) {
            tfree(data); return (E_NOMEM);
        }

        if (irhswoPorts == NULL)
            irhswoPorts = (double*)TMALLOC(double, ckt->CKTmaxEqNum);
        else
            irhswoPorts = (double*)TREALLOC(double, irhswoPorts, ckt->CKTmaxEqNum);

        if (irhswoPorts == NULL) {
            tfree(rhswoPorts);
            tfree(data); return (E_NOMEM);
        }

        ckt->CKTmode = (ckt->CKTmode & MODEUIC) | MODESP;

        // Let's sweep thru all available ports to build Y matrix
        // Y_ij = I_i / V_j | V_k!=j = 0
        // (we have only to modify rhs)

        int vsrcLookupType = CKTtypelook("Vsource");
        int vsrcRoot = -1;
        
        // Get VSRCs root model
        if (ckt->CKTVSRCid == -1)
        {
            for (i = 0; i < DEVmaxnum; i++) {
                if (DEVices[i] && DEVices[i]->DEVacLoad && ckt->CKThead[i] && ckt->CKThead[i]->GENmodType == vsrcLookupType) {

                    vsrcRoot = i;
                    break;
                }
            }
            if (vsrcRoot == -1)
                return (E_NOMOD);

            ckt->CKTVSRCid = vsrcRoot;

            // Now that we have found the model, we may init the Zref and Gn ports
            VSRCspinit(ckt->CKThead[vsrcRoot], ckt, zref, gn, gninv);
        }
        else
            vsrcRoot = ckt->CKTVSRCid;


        // Pre-load everything but RF Ports (these will be updated in the next cycle).
        error = NIspPreload(ckt);
        if (error) return (error);
        
//        error = VSRCsaveNPData(ckt->CKThead[vsrcRoot]);
//        if (error) return (error);

        //Keep a backup copy
        memcpy(rhswoPorts, ckt->CKTrhs,  ckt->CKTmaxEqNum * sizeof(double));
        memcpy(rhswoPorts, ckt->CKTirhs, ckt->CKTmaxEqNum * sizeof(double));

        for (activePort = 1; activePort <= ckt->CKTportCount; activePort++)
        {
            // Copy the backup RHS into CKT's RHS
            memcpy(ckt->CKTrhs, rhswoPorts, ckt->CKTmaxEqNum * sizeof(double));
            memcpy(ckt->CKTirhs, irhswoPorts, ckt->CKTmaxEqNum * sizeof(double));
            ckt->CKTactivePort = activePort;

            // Update only VSRCs
            error = VSRCspupdate(ckt->CKThead[vsrcRoot], ckt);
            if (error)
            {
                tfree(rhswoPorts);
                tfree(irhswoPorts);
                tfree(data);
                deleteSPmatrix(ckt);
                return(error);
            }

            error = NIspSolve(ckt);
            if (error) {
                tfree(rhswoPorts);
                tfree(irhswoPorts);
                tfree(data);
                deleteSPmatrix(ckt);
                UPDATE_STATS(DOING_AC);
                return(error);
            }


#ifdef WANT_SENSE2
            if (ckt->CKTsenInfo && (ckt->CKTsenInfo->SENmode & ACSEN)) {
                long save;
                int save1;

                save = ckt->CKTmode;
                ckt->CKTmode = (ckt->CKTmode & MODEUIC) | MODEDCOP | MODEINITSMSIG;
                save1 = ckt->CKTsenInfo->SENmode;
                ckt->CKTsenInfo->SENmode = ACSEN;
                if (freq == job->SPstartFreq) {
                    ckt->CKTsenInfo->SENacpertflag = 1;
                }
                else {
                    ckt->CKTsenInfo->SENacpertflag = 0;
                }
                error = CKTsenAC(ckt);
                if (error)
                    return (error);
                ckt->CKTmode = save;
                ckt->CKTsenInfo->SENmode = save1;
            }
#endif

            // We have done 1 activated port.
            error = CKTspCalcPowerWave(ckt);


        } //active ports cycle

        // Now we can calculate the full S-Matrix
        CKTspCalcSMatrix(ckt);

        /*
        * Now go with noise cycle, if required
        */
        if (job->SPdoNoise)
        {

            data->delFreq = freq - data->lstFreq;
            data->freq = freq;


            cinit(ckt->CKTNoiseCYmat, 0.0, 0.0);

            for (activePort = 0; activePort < ckt->CKTportCount; activePort++)
            {
                /* the frequency will NOT be stored in array[0]  as before; instead,
                 * it will be given in refVal.rValue (see later)
                 */
                ckt->CKTactivePort = activePort+1;

                NInspIter(ckt, (VSRCinstance*)(ckt->CKTrfPorts[activePort]));   /* solve the adjoint system */
                /* put the solution of the current adjoint system into the storage matrix*/
                int j;
                for (j = 0; j < ckt->CKTmaxEqNum; j++)
                {
                    cplx temp;
                    temp.re = ckt->CKTrhs[j];
                    temp.im = ckt->CKTirhs[j];

                    ckt->CKTadjointRHS->d[activePort][j] = temp;
                }
            }
             /* 
            now we have all the solutions of the adjoint system, we may look into actual
            noise sourches
             */

            error = CKTspnoise(ckt, N_DENS, N_CALC, data);
            if (error)
            {
               tfree(data);
                tfree(rhswoPorts);
                tfree(irhswoPorts);
                deleteSPmatrix(ckt);
                return(error);
            }
            data->lstFreq = freq;
        }

        
#ifdef XSPICE
        /* gtri - modify - wbk - 12/19/90 - Send IPC stuff */

        if (g_ipc.enabled)
            ipc_send_data_prefix(freq);

        error = CKTspDump(ckt, freq, spPlot, job->SPdoNoise);

        if (g_ipc.enabled)
            ipc_send_data_suffix();

        /* gtri - modify - wbk - 12/19/90 - Send IPC stuff */
#else
        error = CKTspDump(ckt, freq, acPlot, job->SPdoNoise));
#endif	
        if (error) {
            UPDATE_STATS(DOING_AC);
            tfree(rhswoPorts);
            tfree(irhswoPorts);
            tfree(data);
            deleteSPmatrix(ckt);
            return(error);
        }
        /*  increment frequency */

        switch (job->SPstepType) {
        case DECADE:
        case OCTAVE:

            /* inserted again 14.12.2001  */
#ifdef HAS_PROGREP
        {
            double endfreq = job->SPstopFreq;
            double startfreq = job->SPstartFreq;
            endfreq = log(endfreq);
            if (startfreq == 0.0)
                startfreq = 1e-12;
            startfreq = log(startfreq);

            if (freq > 0.0)
                SetAnalyse("sp", (int)((log(freq) - startfreq) * 1000.0 / (endfreq - startfreq)));
        }
#endif

        freq *= job->SPfreqDelta;
        if (job->SPfreqDelta == 1) goto endsweep;
        break;
        case LINEAR:

#ifdef HAS_PROGREP
        {
            double endfreq = job->SPstopFreq;
            double startfreq = job->SPstartFreq;
            SetAnalyse("sp", (int)((freq - startfreq) * 1000.0 / (endfreq - startfreq)));
        }
#endif

        freq += job->SPfreqDelta;
        if (job->SPfreqDelta == 0) goto endsweep;
        break;
        default:
            tfree(rhswoPorts);
            tfree(irhswoPorts);
            tfree(data);
            deleteSPmatrix(ckt);
            return(E_INTERN);

        }
    }
endsweep:
    SPfrontEnd->OUTendPlot (spPlot);
    spPlot = NULL;
    UPDATE_STATS(0);
    tfree(rhswoPorts);
    tfree(irhswoPorts);
    deleteSPmatrix(ckt);
    tfree(data);
    return(0);
}


#endif