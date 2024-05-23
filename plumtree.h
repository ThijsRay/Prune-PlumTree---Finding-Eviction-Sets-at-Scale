#pragma once

#include <sys/mman.h>
#include <unistd.h>

//Sysyem params
#define W 16         //Modify according to your machine
#define SetsLLC 8192 //Modify according to your machine
#define THRESHOLD 95 //Modify according to your machine
#define BlockSize 64
#define PageSize 4096

//Algorithm Parameters
#define FW 1
#define BufferSize 20

typedef struct Prune_Args {
  void *head;
  int N_c;
} Prune_Args;

typedef struct Probe_Args {
  void *first;
  void *sec;
  int N1;
  int N2;
} Probe_Args;

typedef struct my_list {
  void *candidates;
  int N_c;
  void *Representatives;
  int N_R;
  void *SeconedHalf;
  int N_Sec;
} State;

//======================Prototypes definitions===========================
void Prime(void *address, int direction);
State Probe(State addresses);
State map_LLC(float LLC_Cover, State addresses);
void ProbeInfo(void *head, void *Rhead, void *tail, void *Rtail, char *MissHit, int size);
State Prune(State addresses);
void PruneInfo(void *head, void *tail, char *MissHit, int NumExp, int size, void *mapping_head);
void menu(void);
int reduction_iterative(State addresses);
Probe_Args probe(void *p, int N_c, char *MissHit);
void External_Voting(void *p, char *MissHit, int direction, int size);
State BuildTrees(State addresses);
int plumtree_main(void);
void plumtree_free(State tmp);
//==========================================================================
