#include <stdint.h>     /* defines uint32_t etc */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// What can I say, I always liked this macro.
#define FOREVER for(;;)

// power of two size of the hash table
#define NUMELEMENTS (1024)
// mask for hashlittle (more efficient than mod)
#define HASHMASK (NUMELEMENTS-1)
// max depth for dependence tree built from given variable name
#define MAXDEFAULT 5

// file format is:
// [depVar @ depFunc * depBB -- reqVar @ reqFunc * reqBB] line1#line2# other profile data
#define DOUT_FORMAT LCOUT_FORMAT
#define LCOUT_FORMAT "%c %[^ @] %*[@] %[^ *] %*[*] %[^ -] %*[-] %[^ @] %*[@] %[^ *] %*[*] %[^]] %*[]] %[^#] %*[#] %[^ #] %*[^:] %*[:] %*[^:] %*[: ] %[^ ] %*[^\n] %*[\n]"

// file format is:
// [depVar @ depFunc * depBB -- reqVar @ reqFunc * reqBB] isLoopCarried line1#line2# ##
#define AUXOUT_FORMAT "%c %[^ @] %*[@] %[^ *] %*[*] %[^ -] %*[-] %[^ @] %*[@] %[^ *] %*[*] %[^]] %*[]] %s %[^#] %*[#] %[^ #] %*[^:] %*[: ] %[^ ]  %*[^\n] %*[\n]"


#define ISLOOPCARRIED 1
#define NOTLOOPCARRIED 0

// function header for hash function from file lookup3.c
uint32_t hashlittle( const void *key, size_t length, uint32_t initval);

// Variable info structure
typedef struct {
      char varName [40];      // Extracted variable name
      char functionName [40]; // Function name
      char bbName [10];       // LLVM basic block name
      int loopCarried;        // 1 if loop-carried, 0 if not
      char line[20];          // Source line number
} varInfo;

// Structure for nodes in requires/requiredBy lists
typedef struct node{
      varInfo myInfo;         // Variable information
      struct node * next;
} node;

// Hashtable entries for every variable
typedef struct tableEntry{
      varInfo myInfo;         // Variable's info
      node * requires;        // requires list for variable
      node * requiredBy;      // required by list for variable
      struct tableEntry * next;
} tableEntry;

FILE * outfile;



//////////////////// Table Construction Functions \\\\\\\\\\\\\\\\\\\\

// Main body of table construction from files
// lconly is whether to only construct with loop-carried data
void buildTable (unsigned lconly);


// Fetches an existing entry with given parameters
// or creates it if it is not found.
tableEntry * createOrGetTableE(char* name, char* functionName, char* bbName, char* line);

// Takes an existing tableEntry and inserts a new "requires" relation for this entry
// with data given for name, function name, basic block name, and line number
int insertReq(tableEntry * insPoint, char* name, char* functionName, char* bbName, int loopCarried, char* line);

// Takes an existing tableEntry and inserts a new "required by" relation for this entry
// with data given for name, function name, basic block name, and line number
int insertDep(tableEntry * insPoint, char* name, char* functionName, char* bbName, int loopCarried, char* line);


//////////////////// Basic Table Display Functions \\\\\\\\\\\\\\\\\\\\

// Main menu for user input
void menu();

// search table for depends on
// gets requires nodes from hash table
void dependsOnWhat(char* name);

// search table for required by
// gets requiredBy nodes from hash table
void requiredByWhat(char* name);

// output data from all nodes starting at node walk
void outputNodes(node * walk);

// lists all variables in a function that can
void listVarsInFn(char* functionName);


/////////////////// Recursive Table Display Functions \\\\\\\\\\\\\\\\\\\

// **********************************
// The following four functions are used in recursively building
// "requires" trees for a given variable
// **********************************

// Takes a given variable name and finds ALL instances of it.
// It then recursively generates all dependences.
void buildDependenceChain(char* name);

// Takes a given variable and function name and finds ALL instances of it.
// It then recursively generates all dependences.
void buildDependenceChainKnownFunc(char* name, char* fnName);

// Recursive function for walking all nodes in an entry for a variable
// Calls buildChain for every variable it finds in the "requires" list
// walk is the start of the list.
// depth is the current depth (only recurses to a fixed max depth)
// "last" variables ensure self-dependences don't cause recurrence
void walkNodes(node * walk, int depth, char* lastName, char* lastFnName, char* lastBbName, char* lastLine);

// Recursive function for finding dependences of variables discovered by
// walkNodes.  Calls walkNodes with the "requires" list
// for every entry it discovers.
void buildChain(char* name, char* fnName, char* bbName, int depth, char* line);

// **********************************
// The following five functions are used in recursively building
// "required by" trees for given information
// **********************************

// Takes a given variable name and finds ALL instances of it.
// It then recursively generates all dependences.
void buildDependenceChain2(char* name);

// Takes a given variable and function name and finds ALL instances of it.
// It then recursively generates all dependences.
void buildDependenceChainKnownFunc2(char* name, char* fnName);

// Takes a given line and function and finds ALL instances of it.
// It then recursively generates all dependences.
void buildDependenceChainLine2 (char* line, char* fn);

// Recursive function for walking all nodes in an entry for a variable
// Calls buildChain2 for every variable it finds in the "required by" list
// walk is the start of the list.
// depth is the current depth (only recurses to a fixed max depth)
// "last" variables ensure self-dependences don't cause recurrence
void walkNodes2(node * walk, int depth, char* lastName, char* lastFnName, char* lastBbName, char* lastLine);

// Recursive function for finding dependences of variables discovered by
// walkNodes.  Calls walkNodes2 with the "required by" list
// for every entry it discovers.
void buildChain2(char* name, char* fnName, char* bbName, int depth, char* line);

void spew();


// stores dependence information
tableEntry ** myTable;
int MAXDEPTH;

int main (int argc, char **argv)
{
   unsigned lc;
   char filename[50];

   if (argc == 2)
      MAXDEPTH = atoi(argv[1]);
   else
      MAXDEPTH = MAXDEFAULT;

   printf("Enter line number of loop of interest: ");
   scanf("%d", &lc);

   lc = readLoopTranslator(lc);

   buildTable(lc);

   sprintf(filename, "Loop%d.LO.result", lc);

   printf("Output generating to file: %s\n", filename);

   outfile = fopen(filename, "w");

   spew();

   //menu();

   printf("Output Successful\n\n");

   return 0;
}




int readLoopTranslator(int loopLine)
{
   FILE *inFile;
   int loopID, transLoopLine, lastID = -1;
   int check;
   char fn[100];

   if(!(inFile = fopen("loops.out", "r")))
   {
      fprintf(stderr, "Failure to open loop cross-reference file loops.out.  Aborting.\n");
      exit(1);
   }
   fscanf(inFile, "%*[^\n] %*[\n]");
   fscanf(inFile, "%*[^\n] %*[\n]");// remove header
   fscanf(inFile, "%d%d%s", &loopID, &transLoopLine, fn);

   while(lastID != loopID)
   {
      if(loopLine == transLoopLine)
      {
	printf("LoopID %d found at line %d in %s.  Enter 1 to use this, 0 to continue: ", loopID, transLoopLine, fn);
         scanf("%d", &check);

         if (check) {
            fclose(inFile);
            return loopID;
         }
      }
      lastID = loopID;
      fscanf(inFile, "%d%d%s", &loopID, &transLoopLine, fn);
   }

   fprintf(stderr, "No valid options discovered for line %d.  Aborting.\n", loopLine);
   exit(2);

}












void buildTable(unsigned loop)
{
  char comment;
   char dependantVar [40];
   char dependantFn [40];
   char dependantBB [10];
   char requiredVar [40];
   char requiredFn [40];
   char requiredBB [10];
   char isLC[5];
   char line1[20] = {'\0', '\0', '\0'};  // corresponds with dependant
   char line2[20] = {'\0', '\0', '\0'};  // corresponds with required
   char thisLoop[8];
   char nul[4] = "nul";

   myTable = calloc(NUMELEMENTS, sizeof(tableEntry*));

   FILE *inFile;

   // Read Loop-Carried file from LAMP read-in pass
   if(!(inFile = fopen("lcout.out", "r")))
   {
      fprintf(stderr, "Failure to open file lcout.out.  Aborting.\n");
      exit(1);
   }

   while(fscanf(inFile, LCOUT_FORMAT, &comment, dependantVar, dependantFn, dependantBB,
                requiredVar, requiredFn, requiredBB, line1, line2, thisLoop) == 10 || comment == '#')
   {
     if (comment == '#')
       {
	 fscanf(inFile, "%*[^\n] %*[\n]");
	 comment = '0';
	 continue;
       }

      // LAMP extractor failed to determine a legitimate variable name (should appear in AUXFILE)
      if (atoi(thisLoop) != loop)
         continue;

      // insert required variable for the dependant variable
      tableEntry * insLocDep = createOrGetTableE(line1, dependantFn, nul, line1);
      insertReq(insLocDep, line2, requiredFn, nul, ISLOOPCARRIED, line2);

      // insert dependant variable in entry for the required variable
      tableEntry * insLocReq = createOrGetTableE(line2, requiredFn, nul, line2);
      insertDep(insLocReq, line1, dependantFn, nul, ISLOOPCARRIED, line1);
      comment = '0';
   }

   fclose(inFile);

   printf("Loop-Carried file COMPLETE\n");


   printf("*********\nTables constructed\n\n");
}


/////////////// TABLE CONSTRUCTION HELPER FUNCTIONS ///////////////////
// Finds entry with given name, fn, bb or create it if not found
// This function is used for building the table
tableEntry * createOrGetTableE(char* name, char* functionName, char* bbName, char* line)
{

   uint32_t hashval = hashlittle(name, strlen(name), 50) & HASHMASK;

   tableEntry * thisHash = myTable[hashval];


   while(thisHash != NULL)
   {
      if((strcmp(name, (thisHash->myInfo).varName) == 0) // name match
         && (strcmp(functionName, (thisHash->myInfo).functionName) == 0) // function match
         && (strcmp(bbName, (thisHash->myInfo).bbName) == 0) // bb match
         && (strcmp(line, (thisHash->myInfo).line) == 0)) // line match
         return thisHash;

      thisHash = thisHash->next;  // keep looking
   }

   tableEntry * oldHead = myTable[hashval];

   // no match so create a new one

   if(!(myTable[hashval] = malloc(sizeof(tableEntry))))
      exit(1);

   thisHash = myTable[hashval];

   strcpy((thisHash->myInfo).varName, name);
   strcpy((thisHash->myInfo).functionName, functionName);
   strcpy((thisHash->myInfo).bbName, bbName);
   strcpy((thisHash->myInfo).line, line);
   (thisHash->myInfo).loopCarried = -1;
   thisHash->next = oldHead;

   return thisHash;

}

int insertReq(tableEntry * insPoint, char* name, char* functionName, char* bbName, int loopCarried, char* line)
{
   node * newNode;
   node * walk = insPoint->requires;

   while (walk)
   {
      if((strcmp(name, (walk->myInfo).varName) == 0) // name match
         && (strcmp(functionName, (walk->myInfo).functionName) == 0) // function match
         && (strcmp(bbName, (walk->myInfo).bbName) == 0) // bb match
         && (strcmp(line, (walk->myInfo).line) == 0))   // line match
         return 0;   // exact same dependence already recorded

      walk = walk->next;
   }

   if(!(newNode = malloc(sizeof(node))))
      exit(1);

   strcpy((newNode->myInfo).varName, name);
   strcpy((newNode->myInfo).functionName, functionName);
   strcpy((newNode->myInfo).bbName, bbName);
   strcpy((newNode->myInfo).line, line);
   (newNode->myInfo).loopCarried = loopCarried;
   newNode->next = insPoint->requires;
   insPoint->requires = newNode;

   return 1;
}

int insertDep(tableEntry * insPoint, char* name, char* functionName, char* bbName, int loopCarried, char* line)
{
   node * newNode;
   node * walk = insPoint->requiredBy;

   while (walk)
   {
      if((strcmp(name, (walk->myInfo).varName) == 0) // name match
         && (strcmp(functionName, (walk->myInfo).functionName) == 0) // function match
         && (strcmp(bbName, (walk->myInfo).bbName) == 0) // bb match
         && (strcmp(line, (walk->myInfo).line) == 0)) // line match
         return 0;   // exact same dependence already recorded

      walk = walk->next;
   }

   if(!(newNode = malloc(sizeof(node))))
      exit(1);

   strcpy((newNode->myInfo).varName, name);
   strcpy((newNode->myInfo).functionName, functionName);
   strcpy((newNode->myInfo).bbName, bbName);
   strcpy((newNode->myInfo).line, line);
   (newNode->myInfo).loopCarried = loopCarried;
   newNode->next = insPoint->requiredBy;
   insPoint->requiredBy = newNode;

   return 1;
}
/////////////////// END TABLE CONSTRUCTION HELPER FUNCTIONS /////////////



void spew()
{
   int i = 0;
   tableEntry * walk;

   while(i < NUMELEMENTS)
   {
      walk = myTable[i];

      while(walk)
      {
         fprintf(outfile, "%s at %s in %s is required by:\n", (walk->myInfo).varName,
                 (walk->myInfo).line, (walk->myInfo).functionName);
         walkNodes2(walk->requiredBy, 0, (walk->myInfo).varName, (walk->myInfo).functionName,
                    (walk->myInfo).bbName, (walk->myInfo).line);// exact same dependence already recorded


         walk = walk->next;
      }

      i++;
   }

}












///////////////////  SIMPLE DISPLAY FUNCTIONS /////////////////////////////
// search table for depends on
// gets requires nodes from hash table
void dependsOnWhat(char* name)
{
   uint32_t hashval = hashlittle(name, strlen(name), 50) & HASHMASK;

   tableEntry * walk = myTable[hashval];

   while (walk != NULL)
   {
      if(strcmp(name, (walk->myInfo).varName) == 0) // name match
      {
         printf("Found %s in function \"%s\" (block %s line %s), flow-dependant on:\n",
                (walk->myInfo).varName, (walk->myInfo).functionName,
                (walk->myInfo).bbName, (walk->myInfo).line);
         outputNodes(walk->requires);// exact same dependence already recorded
      }

      walk = walk->next;
   }

}

// search table for required by
// gets requiredBy nodes from hash table
void requiredByWhat(char* name)
{
   uint32_t hashval = hashlittle(name, strlen(name), 50) & HASHMASK;

   tableEntry * walk = myTable[hashval];

   while (walk != NULL)
   {
      if(strcmp(name, (walk->myInfo).varName) == 0) // name match
      {
         printf("Found %s in function \"%s\" (block %s line %s), feeds:\n",
                (walk->myInfo).varName, (walk->myInfo).functionName,
                (walk->myInfo).bbName, (walk->myInfo).line);
         outputNodes(walk->requiredBy);// exact same dependence already recorded
      }

      walk = walk->next;
   }

}

void outputNodes(node * walk)
{
   while(walk)
   {
      printf(" -- %s in function \"%s\" (block %s line %s)\n",
             (walk->myInfo).varName, (walk->myInfo).functionName,
             (walk->myInfo).bbName, (walk->myInfo).line);

      walk = walk->next;
   }
}

void listVarsInFn(char* functionName)
{
   int i =0;
   tableEntry * walk;



   while(i < NUMELEMENTS)
   {
      walk = myTable[i];

      while(walk)
      {
        if(strcmp(functionName, (walk->myInfo).functionName) == 0) // name match
        {
           printf("%s\n", (walk->myInfo).varName);
        }
        walk = walk->next;
      }

      i++;
   }

}
///////////////// END SIMPLE DISPLAY FUNCTIONS ////////////////






//////// REQUIRES TREES ///////////
void buildDependenceChain(char* name)
{
   uint32_t hashval = hashlittle(name, strlen(name), 50) & HASHMASK;

   tableEntry * walk = myTable[hashval];

   while (walk != NULL)
   {
      if(strcmp(name, (walk->myInfo).varName) == 0) // name match
      {
         printf("%s in function \"%s\" (block %s line %s), flow-dependant on:\n",
                (walk->myInfo).varName, (walk->myInfo).functionName,
                (walk->myInfo).bbName, (walk->myInfo).line);

         walkNodes(walk->requires, 0, (walk->myInfo).varName, (walk->myInfo).functionName,
                   (walk->myInfo).bbName, (walk->myInfo).line);// exact same dependence already recorded
      }

      walk = walk->next;
   }

}

void buildDependenceChainKnownFunc(char* name, char* fnName)
{
   uint32_t hashval = hashlittle(name, strlen(name), 50) & HASHMASK;

   tableEntry * walk = myTable[hashval];

   while (walk != NULL)
   {
      if((strcmp(name, (walk->myInfo).varName) == 0) // name match
         && (strcmp(fnName, (walk->myInfo).functionName) == 0)) // function match
      {
         printf("%s in function \"%s\" (block %s line %s), flow-dependant on:\n",
                (walk->myInfo).varName, (walk->myInfo).functionName,
                (walk->myInfo).bbName, (walk->myInfo).line);

         walkNodes(walk->requires, 0,(walk->myInfo).varName, (walk->myInfo).functionName,
                   (walk->myInfo).bbName, (walk->myInfo).line);// exact same dependence already recorded
      }

      walk = walk->next;
   }


}

void walkNodes(node * walk, int depth, char* lastName, char* lastFnName, char* lastBbName, char* lastLine)
{
   int i;

   if (depth == MAXDEPTH)
      return;

   while(walk)
   {
      for (i = 0; i <= depth; i++)
      {
         putchar(' ');putchar(' ');putchar(' ');
      }

      printf("%s in function \"%s\" (block %s line %s)\n",
             (walk->myInfo).varName, (walk->myInfo).functionName,
             (walk->myInfo).bbName, (walk->myInfo).line);


      if((strcmp(lastName, (walk->myInfo).varName) != 0) // name match
         || (strcmp(lastFnName, (walk->myInfo).functionName) != 0) // function match
         || (strcmp(lastBbName, (walk->myInfo).bbName) != 0) // bb match
         || (strcmp(lastLine, (walk->myInfo).line) != 0))
      {
      buildChain((walk->myInfo).varName, (walk->myInfo).functionName,
                 (walk->myInfo).bbName, depth+1, (walk->myInfo).line);
      }

      walk = walk->next;
   }
}

void buildChain(char* name, char* fnName, char* bbName, int depth, char* line)
{
   uint32_t hashval = hashlittle(name, strlen(name), 50) & HASHMASK;

   tableEntry * walk = myTable[hashval];



   while (walk != NULL)
   {


      if((strcmp(name, (walk->myInfo).varName) == 0) // name match
         && (strcmp(fnName, (walk->myInfo).functionName) == 0) // function match
         && (strcmp(bbName, (walk->myInfo).bbName) == 0) // bb match
         && (strcmp(line, (walk->myInfo).line) == 0))
      {
         walkNodes(walk->requires, depth, name, fnName, bbName, line);// exact same dependence already recorded
      }

      walk = walk->next;
   }


}
//////////////// END REQUIRES TREES ///////////////////





//////////////// REQUIRED BY TREES /////////////////////
void buildDependenceChain2(char* name)
{
   uint32_t hashval = hashlittle(name, strlen(name), 50) & HASHMASK;

   tableEntry * walk = myTable[hashval];

   while (walk != NULL)
   {
      if(strcmp(name, (walk->myInfo).varName) == 0) // name match
      {
         printf("%s in function \"%s\" (block %s line %s), feeds:\n",
                (walk->myInfo).varName, (walk->myInfo).functionName,
                (walk->myInfo).bbName, (walk->myInfo).line);

         walkNodes2(walk->requiredBy, 0, (walk->myInfo).varName, (walk->myInfo).functionName,
                   (walk->myInfo).bbName, (walk->myInfo).line);// exact same dependence already recorded
      }

      walk = walk->next;
   }

}

void buildDependenceChainKnownFunc2(char* name, char* fnName)
{
   uint32_t hashval = hashlittle(name, strlen(name), 50) & HASHMASK;

   tableEntry * walk = myTable[hashval];

   while (walk != NULL)
   {
      if((strcmp(name, (walk->myInfo).varName) == 0) // name match
         && (strcmp(fnName, (walk->myInfo).functionName) == 0)) // function match
      {
         printf("%s in function \"%s\" (block %s line %s), feeds:\n",
                (walk->myInfo).varName, (walk->myInfo).functionName,
                (walk->myInfo).bbName, (walk->myInfo).line);

         walkNodes2(walk->requiredBy, 0,(walk->myInfo).varName, (walk->myInfo).functionName,
                   (walk->myInfo).bbName, (walk->myInfo).line);// exact same dependence already recorded
      }

      walk = walk->next;
   }


}

void buildDependenceChainLine2 (char* line, char* fn)
{
   int i =0;
   tableEntry * walk;



   while(i < NUMELEMENTS)
   {
      walk = myTable[i];

      while(walk)
      {
         if((strcmp(line, (walk->myInfo).line) == 0) // line match
            && (strcmp(fn, (walk->myInfo).functionName) == 0))
         {
            printf("%s\n", (walk->myInfo).varName);
            walkNodes2(walk->requiredBy, 0, (walk->myInfo).varName, (walk->myInfo).functionName,
                      (walk->myInfo).bbName, (walk->myInfo).line);// exact same dependence already recorded

         }
         walk = walk->next;
      }

      i++;
   }
}


void walkNodes2(node * walk, int depth, char* lastName, char* lastFnName, char* lastBbName, char* lastLine)
{
   int i;

   if (depth == MAXDEPTH)
      return;

   while(walk)
   {
      for (i = 0; i <= depth; i++)
      {
         fputc(' ', outfile);fputc(' ', outfile);fputc(' ', outfile);
      }

      fprintf(outfile, "%s in function \"%s\" (block %s line %s)\n",
             (walk->myInfo).varName, (walk->myInfo).functionName,
             (walk->myInfo).bbName, (walk->myInfo).line);


      if((strcmp(lastName, (walk->myInfo).varName) != 0) // name match
         || (strcmp(lastFnName, (walk->myInfo).functionName) != 0) // function match
         || (strcmp(lastBbName, (walk->myInfo).bbName) != 0) // bb match
         || (strcmp(lastLine, (walk->myInfo).line) != 0))
      {
      buildChain2((walk->myInfo).varName, (walk->myInfo).functionName,
                 (walk->myInfo).bbName, depth+1, (walk->myInfo).line);
      }

      walk = walk->next;
   }
}

void buildChain2(char* name, char* fnName, char* bbName, int depth, char* line)
{
   uint32_t hashval = hashlittle(name, strlen(name), 50) & HASHMASK;

   tableEntry * walk = myTable[hashval];



   while (walk != NULL)
   {


      if((strcmp(name, (walk->myInfo).varName) == 0) // name match
         && (strcmp(fnName, (walk->myInfo).functionName) == 0) // function match
         && (strcmp(bbName, (walk->myInfo).bbName) == 0) // bb match
         && (strcmp(line, (walk->myInfo).line) == 0))
      {
         walkNodes2(walk->requiredBy, depth, name, fnName, bbName, line);// exact same dependence already recorded
      }

      walk = walk->next;
   }


}
////////////// END REQUIRED BY TREES ///////////////////
