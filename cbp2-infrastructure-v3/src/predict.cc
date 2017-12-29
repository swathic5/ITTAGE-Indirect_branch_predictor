// predict.cc
// This file contains the main function.  The program accepts a single 
// parameter: the name of a trace file.  It drives the branch predictor
// simulation by reading the trace file and feeding the traces one at a time
// to the branch predictor.

#include <stdio.h>
#include <stdlib.h>
#include <string.h> // in case you want to use e.g. memset
#include <assert.h>

#include "branch.h"
#include "trace.h"
#include "predictor.h"
#include "my_predictor.h"

extern long long int trace_instructions, trace_branches;
extern double instructions_per_branch;

void print_stats (long long int dmiss, long long int tmiss) {
	printf ("%lld instructions; %0.3f IPB; %0.3f direction MPKI; %0.3f indirect MPKI\n", trace_instructions, instructions_per_branch, 1000.0 * (dmiss / (double) trace_instructions), 1000.0 * (tmiss / (double) trace_instructions));
	fflush (stdout);
}

int main (int argc, char *argv[]) {

	// make sure there is one parameter

	if (argc != 2) {
		fprintf (stderr, "Usage: %s <filename>.gz\n", argv[0]);
		exit (1);
	}

	// open the trace file for reading

	init_trace (argv[1]);

	// initialize competitor's branch prediction code

	branch_predictor *p = new my_predictor ();

	// some statistics to keep, currently just for conditional branches

	long long int 
		last_instructions = 0,
		tmiss = 0, 	// number of target mispredictions
		dmiss = 0; 	// number of direction mispredictions

	for (;;) {

		// get a trace

		trace *t = read_trace ();

		// NULL means end of file

		if (!t) break;

		// send this trace to the competitor's code for prediction

		branch_update *u = p->predict (t->bi);

		// collect statistics for a conditional branch trace

		if (t->bi.br_flags & BR_CONDITIONAL) {
			unsigned int z = t->bi.address;
			z &= 0x7fffffff;
			if (t->taken) z |= 0x80000000;

			// count a direction misprediction

			dmiss += u->direction_prediction () != t->taken;
		} 

		// indirect branch prediction

		if (t->bi.br_flags & BR_INDIRECT) {
			// count a target misprediction

			tmiss += u->target_prediction () != t->target;
		}

		// update competitor's state

		p->update (u, t->taken, t->target);

		if (trace_instructions - last_instructions > 100000000) {
			print_stats (dmiss, tmiss);
			last_instructions = trace_instructions;
		}
	}

	// done reading traces

	end_trace ();

	// give final mispredictions per kilo-instruction and exit.
	// the original CBP2 traces have exactly 100,000,000 instructions.
	// newer traces update the trace reader with their instruction count
	if (trace_instructions == 0) {
		trace_instructions = 100000000;
		instructions_per_branch = trace_instructions / (double) trace_branches;
	} else
		trace_instructions = instructions_per_branch * trace_branches;
	print_stats (dmiss, tmiss);
	delete p;
	exit (0);
}
