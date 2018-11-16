#ifndef __STATEMACHINE_H__
#define __STATEMACHINE_H__

struct statemachine_state;
struct statemachine_runtime;

enum statemachine_type {
	MOORE = 0,
	MEALEY,
};

typedef void (state_outfunc)(const struct statemachine_state *,
			     struct statemachine_runtime *);

struct statemachine_state {
	/* There shall be as many next states as are the valid events */
	const int *next_states;
	/* 
	 * This array contains one entry for moore machines, one entry per
	 * valid event for mealey machines
	 */
	state_outfunc **out;
};

struct statemachine_runtime {
	int curr_state;
	void *priv;
};

struct statemachine {
	enum statemachine_type type;
	int nstates;
	const struct statemachine_state *states;
	int nevents;
	/* We may have more instances of the same state machine */
	struct statemachine_runtime **runtimes;
	int nruntimes;
};

extern int init_statemachine(const struct statemachine *, int initial_state);
extern int feed_statemachine(const struct statemachine *,
			     struct statemachine_runtime *, int event);

#endif /* __STATEMACHINE_H__ */
