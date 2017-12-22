/********************************************************************
Count-Min Sketches

G. Cormode 2003,2004

Updated: 2004-06 Added a floating point sketch and support for 
                 inner product point estimation
Initial version: 2003-12

This work is licensed under the Creative Commons
Attribution-NonCommercial License. To view a copy of this license,
visit http://creativecommons.org/licenses/by-nc/1.0/ or send a letter
to Creative Commons, 559 Nathan Abbott Way, Stanford, California
94305, USA. 
*********************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include "prng.h"
#include "massdal.h"
#include "countmin.h"

/* Code modified from implementations found on
 * https://www.cs.rutgers.edu/~muthu/massdal-code-index.html
 * 
 * Comments adapted from comments in
 * https://github.com/laserson/dsq/blob/master/dsq.py
 *
 * Reference paper http://dx.doi.org/10.1016/j.jalgor.2003.12.001
 *
 * This object requires knowledge of the domain of possible values.  This
 * domain is split into dyadic intervals on a binary tree of specified depth
 * (num_levels).  The precision of the result depends on the size of the
 * smallest dyadic interval over the given domain.
 */

/* CMH_Init: init a hierarchical set of sketches with domain and precision information.
        
 * The accuracy of the estimator is limited by the size of the smallest
 * dyadic subdivision of the domain.  So if the domain is [0, 1], and
 * num_levels is set to 10, the smallest subdivision has size 2^(-9).

 * epsilon should be set to the allowable error in the estimate.  (Note
 * that it must be compatible with the num_levels.  If there are not enough
 * levels to achieve the necessary accuracy, this won't work.)

 * delta should be set to the probability of getting a worse estimate
 * (i.e., something small, say, 0.05)

 * The three precision parameters, num_levels, epsilon, and delta
 * ultimately define how much memory is necessary to store the data
 * structures.  There is one CMSketch per level, and each sketch has a
 * width and depth defined by epsilon and delta, as described in the paper.

 * Args:
 *   lower_bound: float lower bound of domain
 *   upper_bound: float upper bound of domain
 *   num_levels: int number of levels in binary tree dyadic partition of
 *       the domain.
 *   epsilon: float amount of error allowed in resulting rank
 *   delta: float probability of error exceeding epsilon accuracy
 */
CMH_type * CMH_Init(int width, int depth, int U, int gran) {
    CMH_type * cmh;
    int i, j, k;
    prng_type * prng;

    if (U <= 0 || U > 32) return NULL;
    // U is the log size of the universe in bits

    if (gran > U || gran < 1) return NULL;
    // gran is the granularity to look at the universe in 
    // check that the parameters make sense...

    cmh = (CMH_type *) malloc(sizeof(CMH_type));

    prng = prng_Init(-12784, 2);
    // initialize the generator for picking the hash functions

    if (cmh && prng) {
        cmh->depth = depth;
        cmh->width = width;
        cmh->count = 0;
        cmh->U = U;
        cmh->gran = gran;
        cmh->levels = (int)ceil(((double) U) / ((double) gran)); /* would be 32 */
        for (j = 0; j < cmh->levels; j++) {
            if ((1u << (cmh->gran * j)) <= cmh->depth * cmh->width) {
                cmh->freelim = j;
            } else {
                break;
            }
        }
        //find the level up to which it is cheaper to keep exact counts
        cmh->freelim = cmh->levels - cmh->freelim;
        /* cmh->freelim to 31 are levels keeping exact counts */
        
        cmh->counts = (int **) calloc(sizeof(int *), 1 + cmh->levels);
        cmh->hasha = (unsigned int **)calloc(sizeof(unsigned int *), 1 + cmh->levels);
        cmh->hashb = (unsigned int **)calloc(sizeof(unsigned int *), 1 + cmh->levels);
        j = 1;
        for (i = cmh->levels - 1; i >= 0; i--)
        {
            if (i >= cmh->freelim)
            { // allocate space for representing things exactly at high levels
                cmh->counts[i] = calloc(1 << (cmh->gran * j), sizeof(int));
                j++;
                cmh->hasha[i] = NULL;
                cmh->hashb[i] = NULL;
            }
            else 
            { // allocate space for a sketch
                cmh->counts[i] = (int *)calloc(sizeof(int), cmh->depth * cmh->width);
                cmh->hasha[i] = (unsigned int *)calloc(sizeof(unsigned int), cmh->depth);
                cmh->hashb[i] = (unsigned int *)calloc(sizeof(unsigned int), cmh->depth);

                if (cmh->hasha[i] && cmh->hashb[i])
                    for (k = 0; k < cmh->depth; k++)
                    { // pick the hash functions
                        cmh->hasha[i][k] = prng_int(prng) & MOD;
                        cmh->hashb[i][k] = prng_int(prng) & MOD;
                    }
            }
        }
    } else {
        fprintf(stderr, "CMH_Init failed\n");
        return NULL;
    }
    return cmh;
}

// free up the space 
void CMH_Destroy(CMH_type * cmh) {
    int i;
    if (!cmh) return;
    for (i = 0; i < cmh->levels; i++) {
        if (i >= cmh->freelim) {
            free(cmh->counts[i]);
        } else {
            free(cmh->hasha[i]);
            free(cmh->hashb[i]);
            free(cmh->counts[i]);
        }
    }
    free(cmh->counts);
    free(cmh->hasha);
    free(cmh->hashb);
    free(cmh);
    cmh = NULL;
}

// update with a new value item and increment cmh->count by diff
void CMH_Update(CMH_type * cmh, unsigned int item, int diff) {
    int i, j, offset;

    if (!cmh) return;
    cmh->max = max(item, cmh->max);
    //printf("COUNT-MIN: maximum observation ever seen = %d\n", cmh->max);
    cmh->count += diff;
    for (i = 0; i < cmh->levels; i++)
    {
        offset = 0;
        // printf("DEBUG: level %d\n", i);
        if (i >= cmh->freelim) {
            // printf("DEBUG: update exact counts at level %d\n", i);
            cmh->counts[i][item] += diff;
            // keep exact counts at high levels in the hierarchy  
        }
        else {
            // printf("DEBUG: update the sketch at level %d\n", i);
            for (j = 0; j < cmh->depth; j++) {
                cmh->counts[i][(hash31(cmh->hasha[i][j], cmh->hashb[i][j], item) 
                    % cmh->width) + offset] += diff;
                // this can be done more efficiently if the width is a power of two
                offset += cmh->width;
                /* 2D array represented as 1D array so offset needs to be
                   incremented by cmh->width */
            }
        }
        item >>= cmh->gran;
    }
}

// return the size used in bytes
int CMH_Size(CMH_type * cmh) {
    int counts, hashes, admin,i;
    if (!cmh) return 0;
    admin = sizeof(CMH_type);
    counts = cmh->levels * sizeof(int **);
    for (i = 0; i < cmh->levels; i++)
    if (i >= cmh->freelim)
      counts += (1 << (cmh->gran * (cmh->levels - i))) * sizeof(int);
    else
      counts += cmh->width * cmh->depth * sizeof(int);
    hashes = (cmh->levels - cmh->freelim) * cmh->depth * 2 * sizeof(unsigned int);
    hashes += (cmh->levels) * sizeof(unsigned int *);
    return admin + hashes + counts;
}

// return an estimate of item at level depth
int CMH_count(CMH_type * cmh, int depth, int item) {
    int j;
    int offset;
    int estimate;

    if (depth >= cmh->levels) return cmh->count;
    if (depth >= cmh->freelim) return cmh->counts[depth][item];
    // else, use the appropriate sketch to make an estimate
    offset=0;
    estimate = cmh->counts[depth][(hash31(cmh->hasha[depth][0], cmh->hashb[depth][0],item) 
        % cmh->width) + offset];
    for (j = 1; j < cmh->depth; j++) {
        offset += cmh->width;
        estimate = min(estimate,
            cmh->counts[depth][(hash31(cmh->hasha[depth][j], cmh->hashb[depth][j],item) 
            % cmh->width) + offset]);
    }
    return estimate;
}

// compute a range sum: 
// start at lowest level
// compute any estimates needed at each level
// work upwards
int CMH_Rangesum(CMH_type * cmh, int start, int end) {
    int leftend, rightend, i, depth, result, topend;

    topend = 1 << cmh->U;
    end = min(topend, end);
    if ((end > topend) && (start == 0))
        return cmh->count;

    end += 1; // adjust for end effects
    result = 0;
    for (depth = 0; depth <= cmh->levels; depth++)
    {
        if (start == end) break;
        if ((end - start + 1) < (1 << cmh->gran))
        { // at the highest level, avoid overcounting 
            for (i = start; i < end; i++)
                result += CMH_count(cmh, depth, i);
            break;
        }
        else
        {  // figure out what needs to be done at each end
            leftend = (((start >> cmh->gran) + 1) << cmh->gran) - start;
            rightend = (end) - ((end >> cmh->gran) << cmh->gran);
            if ((leftend > 0) && (start < end))
                for (i = 0; i < leftend; i++)
                {
                    result += CMH_count(cmh, depth, start + i);
                }
            if ((rightend > 0) && (start < end))
                for (i = 0; i < rightend; i++)
                {
                    result += CMH_count(cmh, depth, end - i - 1);
                }
            start = start >> cmh->gran;
            if (leftend > 0) start++;
            end = end >> cmh->gran;
        }
    }
    return result;
}

// find a range starting from zero that adds up to sum
int CMH_FindRange(CMH_type * cmh, int sum) {
    unsigned long low, high, mid = 0, est;
    int i;

    if (cmh->count < sum) return cmh->max;
    low = 0;
    high = 1 << cmh->U;
    for (i = 0; i < cmh->U; i++) {
        mid = (low + high) / 2;
        est = CMH_Rangesum(cmh, 0, mid);
        if (est > sum)
            high = mid;
        else
            low = mid;
    }
    return mid;
}

// find a range starting from the right hand side that adds up to sum
int CMH_AltFindRange(CMH_type * cmh, int sum) {
    unsigned long low, high, mid = 0, est, top;
    int i;

    if (cmh->count < sum) return cmh->max;
    low = 0;
    top = 1 << cmh->U;
    high = top;
    for (i = 0; i < cmh->U; i++)
    {
        mid = (low + high) / 2;
        est = CMH_Rangesum(cmh, mid, top);
        if (est < sum)
            high = mid;
        else
            low = mid;
    }
    return mid;
}

// find a quantile by doing the appropriate range search
int CMH_Quantile(CMH_type * cmh, float frac) {
    if (frac < 0) return 0;
    if (frac > 1) return 1 << cmh->U;
    int res = (CMH_FindRange(cmh, cmh->count * frac) 
        + CMH_AltFindRange(cmh, cmh->count * (1 - frac))) / 2;
    // each result gives a lower/upper bound on the location of the quantile
    // with high probability, these will be close: only a small number of values
    // will be between the estimates.

    //printf("COUNT-MIN: %f-percentile = %d\n", frac, res);
    return res; 
}
