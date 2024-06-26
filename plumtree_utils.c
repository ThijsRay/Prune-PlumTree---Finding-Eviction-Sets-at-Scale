#define _GNU_SOURCE

#include <err.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "plumtree_utils.h"

extern void *CandAddressesPool, *RepAddressesPool;
extern void **GarbageReps, **GarbageCands;
extern int GarbageRepsIDX, GarbageCandsIDX, MappingIdx, Stride, S, mappingSize_array[BufferSize];
extern int *EvictionSetSize;
extern void ***Mapping;
extern clock_t time_array[BufferSize];

unsigned long long my_rand(unsigned long long limit) {
  return ((unsigned long long)(((unsigned long long)rand() << 48) ^ ((unsigned long long)rand() << 32) ^
                               ((unsigned long long)rand() << 16) ^ (unsigned long long)rand())) %
         limit;
}

double nCr(int n, int r) {
  // Since nCr is same as nC(n-r)
  // To decrease number of iterations
  if (r > n / 2)
    r = n - r;

  double answer = 1;
  for (int i = 1; i <= r; i++) {
    answer *= (n - r + i);
    answer /= i;
  }

  return answer;
}

// function to calculate binomial r.v. probability
float binomialProbability(int n, int k, float p) { return nCr(n, k) * pow(p, k) * pow(1 - p, n - k); }

float probabilityToBeEvictionSet(float p, int N) {
  float dist = 0;
  for (int k = 0; k < W; k++) {
    dist = dist + binomialProbability(N, k, p);
  }
  return 1 - dist;
}

void guard(void) {
  __asm__ volatile("mfence\n"
                   "lfence\n");
}

void flush(void *head) {
  void *p, *pp;
  p = head;
  do {
    pp = p;
    p = LNEXT(p);
    guard();
    clflush(pp);
    guard();
  } while (p != (void *)head);
  guard();
  clflush(p);
  guard();
}

void set_cpu(void) {
  cpu_set_t set;
  CPU_ZERO(&set);   // clear cpu mask
  CPU_SET(4, &set); // set cpu 0
  sched_setaffinity(0, sizeof(cpu_set_t), &set);
}

void collectReps(void *head) {
  void *p = head;
  do {
    GarbageReps[GarbageRepsIDX] = p;
    GarbageRepsIDX++;
    p = LNEXT(p);
  } while (p != (void *)head);
  //flush(head);
}

void collectCands(void *head) {
  void *p = head;
  do {
    GarbageCands[GarbageCandsIDX] = p;
    GarbageCandsIDX++;
    p = LNEXT(p);
  } while (p != (void *)head);
  //flush(head);
}

void mergeLists(void *first, void *sec) {
  void *firstTail, *secTail;
  firstTail = LNEXT(NEXTPTR(first));
  secTail = LNEXT(NEXTPTR(sec));
  LNEXT(firstTail) = sec;
  LNEXT(secTail) = first;
  LNEXT(OFFSET(first, sizeof(void *))) = secTail;
  LNEXT(OFFSET(sec, sizeof(void *))) = firstTail;
}

void *getPointer(void *head, int position) {
  void *p;
  int cnt = 0;
  p = head;
  do {
    guard();
    p = LNEXT(p);
    cnt++;
    if (cnt == position)
      return p;
  } while (p != (void *)head);
  guard();
  return NULL;
}

void memoryaccess(void *address, int direction) {
  void *p, *pp;
  p = address;
  pp = p;
  do {
    guard();
    if (direction)
      p = LNEXT(p);
    else
      p = LNEXT(NEXTPTR(p));
  } while (p != (void *)pp);
}

/*void memoryaccess(void* head, int direction) {
    void* p;    
    __asm__ __volatile__ (
        "mov %[head], %[p]\n"   // Move the head pointer to p
        "mov %[p], %%rax\n"     // Move p to rax
        "mov %[direction], %%ecx\n"  // Move direction to ecx
        "cmp $0, %%ecx\n"       // Compare direction with 0
        "jne forward\n"         // Jump to forward if direction is not equal to 0
        "backward:\n"
        "add $8, %%rax\n"       // Increment p by 8
        "mov (%%rax), %%rax\n"  // Load the value at address pointed by rax into rax
        "cmp %[head], %%rax\n"  // Compare the value in rax with the head pointer
        "jne backward\n"         // Jump back to forward if the values are not equal
        "jmp exit\n"
		"forward:\n"
        "mov (%%rax), %%rax\n"  // Load the value at address pointed by rax into rax
        "cmp %[head], %%rax\n"  // Compare the value in rax with the head pointer
        "jne forward\n"         // Jump back to forward if the values are not equal
		"exit:\n"
        : [p] "=r" (p)
        : [head] "r" (head), [direction] "r" (direction)  // Input: head and direction are used as register operands
        : "%rax", "%ecx"        // Clobbered registers: rax and rcx are modified in the assembly code
    );
}*/

/*
void Prune_memoryaccess(void* start, void* stop) {
    void* p;  
    __asm__ volatile (
        "mov %[start], %[p]\n"   // Move the start pointer to p
        "mov %[p], %%rax\n"     // Move p to rax
        "mov %[stop], %%rbx\n"  // Move stop to rbx
		"fw:\n"
        "mov (%%rax), %%rax\n"  // Load the value at address pointed by rax into rax
        "cmp %%rax, %%rbx\n"    // Compare the value in rax with stop
        "jne fw\n"         // Jump back to forward if the values are not equal
        : [p] "=r" (p)
        : [start] "r" (start), [stop] "r" (stop)  // Input: start and stop are used as register operands
        : "%rax", "%rbx"        // Clobbered registers: rax and rbx are modified in the assembly code
    );
}*/

void Prune_memoryaccess(void *start, void *stop) {
  //Only FW direction
  void *p;
  p = start;
  do {
    guard();
    p = LNEXT(p);
  } while (p != (void *)stop);
  guard();
}

State logsGarbege(void) { //FILE *file
  State addresses;
  for (int i = 0; i < GarbageCandsIDX; i++) {
    LNEXT(GarbageCands[i]) = GarbageCands[(i + 1) % GarbageCandsIDX];
    LNEXT(OFFSET(GarbageCands[i], sizeof(void *))) =
        GarbageCands[(i + GarbageCandsIDX - 1) % GarbageCandsIDX];
  }
  for (int i = 0; i < GarbageRepsIDX; i++) {
    LNEXT(GarbageReps[i]) = GarbageReps[(i + 1) % GarbageRepsIDX];
    LNEXT(OFFSET(GarbageReps[i], sizeof(void *))) = GarbageReps[(i + GarbageRepsIDX - 1) % GarbageRepsIDX];
  }

  addresses.candidates = GarbageCands[0];
  addresses.N_c = GarbageCandsIDX;
  addresses.Representatives = GarbageReps[0];
  addresses.N_R = GarbageRepsIDX;
  //flush(addresses.candidates);
  //flush(addresses.Representatives);
  GarbageCandsIDX = 0;
  GarbageRepsIDX = 0;
  return addresses;
}

int CheckResult(void) {
  void *x, *head;
  int cnt = 0;
  for (int i = 0; i < MappingIdx; i++) {
    for (int j = 0; j < EvictionSetSize[i]; j++)
      LNEXT(Mapping[i][j]) = Mapping[i][(j + 1) % EvictionSetSize[i]]; //cyclic list of lines.
    for (int j = 0; j < EvictionSetSize[i]; j++)
      LNEXT(OFFSET(Mapping[i][j], sizeof(void *))) =
          Mapping[i][(j + EvictionSetSize[i] - 1) % EvictionSetSize[i]]; //Reverse cyclic list of lines.

    head = Mapping[i][0];
    x = LNEXT(NEXTPTR(head));
    if (!checkEviction(head, x, x)) {
      cnt++;
    }
  }
  return cnt;
}

State InitData(int N_c, int N_R) {
  //Init data structure
  State addresses;
  void **candidates = (void **)malloc(N_c * sizeof(void *));
  if (candidates == NULL) {
    err(EXIT_FAILURE, "Failed to allocate candiates");
  }
  void **Representatives = (void **)malloc(N_R * sizeof(void *));
  if (Representatives == NULL) {
    err(EXIT_FAILURE, "Failed to allocate candiates");
  }
  addresses.N_c = N_c;
  addresses.N_R = N_R;

  //Collect addresses.
  for (int i = 0; i < N_c; i++)
    candidates[i] = &((char *)CandAddressesPool)[i * Stride];
  for (int i = 0; i < N_R; i++)
    Representatives[i] = &((char *)RepAddressesPool)[i * Stride];

  //Cyclic lists.
  for (int i = 0; i < N_c; i++) {
    LNEXT(candidates[i]) = candidates[(i + 1) % N_c];
    LNEXT(OFFSET(candidates[i], sizeof(void *))) = candidates[(i + N_c - 1) % N_c];
    if (i < N_R) {
      LNEXT(Representatives[i]) = Representatives[(i + 1) % N_R];
      LNEXT(OFFSET(Representatives[i], sizeof(void *))) = Representatives[(i + N_R - 1) % N_R];
    }
  }

  addresses.candidates = candidates[0];
  addresses.Representatives = Representatives[0];
  //flush(addresses.candidates);
  //flush(addresses.Representatives);
  free(candidates);
  free(Representatives);
  return addresses;
}

void collectEvictionSet(State addresses) {
  void *p;
  Mapping[MappingIdx] = (void **)malloc((addresses.N_c + addresses.N_R) * sizeof(void *));
  if (Mapping[MappingIdx] == NULL) {
    err(EXIT_FAILURE, "Failed to allocate Mapping[MappingIdx]");
  }
  EvictionSetSize[MappingIdx] = addresses.N_c + addresses.N_R;
  p = addresses.candidates;
  for (int i = 0; i < addresses.N_c; i++) {
    Mapping[MappingIdx][i] = p;
    p = LNEXT(p);
  }
  p = addresses.Representatives;
  for (int i = 0; i < addresses.N_R; i++) {
    Mapping[MappingIdx][i + addresses.N_c] = p;
    p = LNEXT(p);
  }
  //for(int i=0;i<EvictionSetSize[MappingIdx];i++) clflush(Mapping[MappingIdx][i]);
  MappingIdx++;
}

int checkEviction(void *head, void *x, void *pp) { //PP-> if checkResult pp=x, if reduction pp=head
  void *p;
  Prime(head, FW);
  flush(head);
  memaccesstime((void *)x);
  memaccesstime((void *)x);
  memaccesstime((void *)x);
  for (int i = 0; i < 3; i++) {
    p = head;
    do {
      guard();
      p = LNEXT(p);
    } while (p != (void *)pp);
  }
  if (memaccesstime((void *)x) > THRESHOLD) {
    //flush(head);
    return 1;
  } else {
    //flush(head);
    return 0;
  }
}

char *printMapping(void) {
  char *buf;
  size_t size;
  FILE *f = open_memstream(&buf, &size);

  for (int set = 0; set < MappingIdx; set++) {
    char *str;
    asprintf(&str, "Eviction set %d:\n", set);
    fputs(str, f);
    free(str);
    for (int i = 0; i < EvictionSetSize[set]; i++) {
      asprintf(&str, "%d) Add: %p  set: %ld\n", i, Mapping[set][i],
               (intptr_t)LNEXT(OFFSET(Mapping[set][i], 3 * sizeof(void *))));
      fputs(str, f);
      free(str);
    }
  }
  fclose(f);
  return buf;
}

void *getMappingHead(void) {
  if (MappingIdx == 0)
    return NULL;
  void **EvictionSets = (void **)malloc(MappingIdx * W * sizeof(void *));
  if (EvictionSets == NULL) {
    err(EXIT_FAILURE, "Failed to allocate EvictionSets");
  }
  void *tmp;
  for (int set = 0; set < MappingIdx; set++)
    for (int line = 0; line < W; line++)
      EvictionSets[set * W + line] = Mapping[set][line];
  for (int j = 0; j < MappingIdx * W; j++)
    LNEXT(EvictionSets[j]) = EvictionSets[(j + 1) % (MappingIdx * W)]; //cyclic list of lines.
  for (int j = 0; j < MappingIdx * W; j++)
    LNEXT(OFFSET(EvictionSets[j], sizeof(void *))) =
        EvictionSets[(j + (MappingIdx * W) - 1) % (MappingIdx * W)]; //Reverse cyclic list of lines.
  tmp = EvictionSets[0];
  //flush(tmp);
  free(EvictionSets);
  return tmp;
}

Probe_Args remove_congrunt_addresses(void *head, int size) {
  Probe_Args probe_args;
  void *tail, *MappingHead = getMappingHead();
  char *MissHit = (char *)calloc(size, sizeof(char));
  if (MissHit == NULL) {
    err(EXIT_FAILURE, "Failed to allocate MissHit");
  }
  int NumExp = 4;

  tail = LNEXT(NEXTPTR(head));
  PruneInfo(head, tail, MissHit, NumExp, size, MappingHead);
  probe_args = probe(head, size, MissHit);
  printf("Removed %d addresses\n", probe_args.N2);
  free(MissHit);
  return probe_args;
}

State prepareForMapping(void) {
  State addresses;
  srand(time(NULL));
  float p = 1 / (float)S;
  int N_c = S;
  int N_R = ceil(log(0.01) / log(1 - p)); //0.01 => tolerance/100
  while (probabilityToBeEvictionSet(p, N_c) < 0.99)
    N_c = N_c + 4 * W; //0.99 => LLC_Cover/100

  printf("The size of the candidate set is: %.5f times the size of the LLC (S*w)\n", N_c / (float)(S * W));
  printf("The size of the Representatives set is: %.5f  times the number of sets (S)\n", N_R / (float)S);

  Mapping = (void ***)malloc(S * sizeof(void **));
  if (Mapping == NULL) {
    err(EXIT_FAILURE, "Failed to allocate Mapping");
  }
  EvictionSetSize = (int *)malloc(S * sizeof(int));
  if (EvictionSetSize == NULL) {
    err(EXIT_FAILURE, "Failed to allocate EvictionSetSize");
  }
  MappingIdx = 0;

  //Collect pool of addresses
  CandAddressesPool = mmap(NULL, N_c * Stride, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, 0,
                           0); //Maybe use (void*)(intptr_t)rand() instead of NULL
  if (CandAddressesPool == MAP_FAILED) {
    err(EXIT_FAILURE, "Failed to mmap CandAddressesPool");
  }
  RepAddressesPool = mmap(NULL, N_R * Stride, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, 0,
                          0); //Maybe use (void*)(intptr_t)rand() instead of NULL
  if (RepAddressesPool == MAP_FAILED) {
    err(EXIT_FAILURE, "Failed to mmap RepAddressesPool");
  }
  addresses = InitData(N_c, N_R);

  GarbageCands = (void **)malloc(addresses.N_c * sizeof(void *));
  if (GarbageCands == NULL) {
    err(EXIT_FAILURE, "Failed to allocate GarbageCands");
  }
  GarbageReps = (void **)malloc(addresses.N_R * sizeof(void *));
  if (GarbageReps == NULL) {
    err(EXIT_FAILURE, "Failed to allocate GarbageReps");
  }

  return addresses;
}

void statistics(int NumExp, int AVGmappingSize, double AVGtime) {
  double AVGtime_array[BufferSize] = {0}, AVGmappingSize_array[BufferSize] = {0};

  for (int k = 0; k < BufferSize; k++) {
    AVGtime_array[k] = (double)time_array[k] / (CLOCKS_PER_SEC * NumExp);
    AVGmappingSize_array[k] = (double)mappingSize_array[k] / NumExp;
    printf("%d). time: %f  mapping size: %f\n", k, AVGtime_array[k], AVGmappingSize_array[k]);
  }

  AVGmappingSize /= NumExp;
  AVGtime /= NumExp;
  printf("AVG mapping size: %d\n", AVGmappingSize);
  printf("AVG mapping time: %f\n", AVGtime);
}
