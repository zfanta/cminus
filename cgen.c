/****************************************************/
/* File: cgen.c                                     */
/* The code generator implementation                */
/* for the TINY compiler                            */
/* (generates code for the TM machine)              */
/* Compiler Construction: Principles and Practice   */
/* Kenneth C. Louden                                */
/****************************************************/

#include "globals.h"
#include "symtab.h"
#include "code.h"
#include "cgen.h"

/* tmpOffset is the memory offset for temps
   It is decremented each time a temp is
   stored, and incremeted when loaded again
*/
static int tmpOffset = 0;
static int tmpSize = 0;
static int numberOfArguments = 0;

int forFunctionTable = 0;
int locMain;

static char* localNameStack[1024];
static int localNameStackIndex = 1024;
static char* parameterStack[1024];
static int parameterStackIndex = 1024;

/* prototype for internal recursive code generator */
static void cGen (TreeNode * tree);
static int pushArguments(int depth, TreeNode * tree);
static void pushParameters(char *name);
static int getLocalNameOffset(char *name);
static int getParameterOffset(char *name);
static void insertFunction(int functionLocation, char *name);

/* Procedure genStmt generates code at a statement node */
static void genStmt( TreeNode * tree)
{ TreeNode * p1, * p2, * p3;
  int savedLoc1,savedLoc2,currentLoc;
  int loc;
  switch (tree->kind.stmt) {

      case IfK :
         if (TraceCode) emitComment("-> if") ;
         p1 = tree->child[0] ;
         p2 = tree->child[1] ;
         p3 = tree->child[2] ;
         /* generate code for test expression */
         cGen(p1);
         savedLoc1 = emitSkip(1) ;
         emitComment("if: jump to else belongs here");
         /* recurse on then part */
         cGen(p2);
         savedLoc2 = emitSkip(1) ;
         emitComment("if: jump to end belongs here");
         currentLoc = emitSkip(0) ;
         emitBackup(savedLoc1) ;
         emitRM_Abs("JEQ",ac,currentLoc,"if: jmp to else");
         emitRestore() ;
         /* recurse on else part */
         cGen(p3);
         currentLoc = emitSkip(0) ;
         emitBackup(savedLoc2) ;
         emitRM_Abs("LDA",pc,currentLoc,"jmp to end") ;
         emitRestore() ;
         if (TraceCode)  emitComment("<- if") ;
         break; /* if_k */

      case RepeatK:
         if (TraceCode) emitComment("-> repeat") ;
         p1 = tree->child[0] ;
         p2 = tree->child[1] ;
         savedLoc1 = emitSkip(0);
         emitComment("repeat: jump after body comes back here");
         /* generate code for body */
         cGen(p1);
         /* generate code for test */
         cGen(p2);
         emitRM_Abs("JEQ",ac,savedLoc1,"repeat: jmp back to body");
         if (TraceCode)  emitComment("<- repeat") ;
         break; /* repeat */

      case AssignK:
         if (TraceCode) emitComment("-> assign") ;
         /* generate code for rhs */
         cGen(tree->child[0]);
         /* now store value */
         loc = st_lookup(tree->attr.name);
         emitRM("ST",ac,loc,gp,"assign: store value");
         if (TraceCode)  emitComment("<- assign") ;
         break; /* assign_k */

      case ReadK:
         emitRO("IN",ac,0,0,"read integer value");
         loc = st_lookup(tree->attr.name);
         emitRM("ST",ac,loc,gp,"read: store value");
         break;
      case WriteK:
         /* generate code for expression to write */
         cGen(tree->child[0]);
         /* now output it */
         emitRO("OUT",ac,0,0,"write ac");
         break;
      default:
         break;
    }
} /* genStmt */

/* Procedure genExp generates code at an expression node */
static void genExp( TreeNode * tree)
{ int loc;
  TreeNode * p1, * p2;
  switch (tree->kind.exp) {

    case ConstK :
      if (TraceCode) emitComment("-> Const") ;
      /* gen code to load integer constant using LDC */
      emitRM("LDC",ac,tree->attr.val,0,"load const");
      if (TraceCode)  emitComment("<- Const") ;
      break; /* ConstK */
    
    case IdK :
      if (TraceCode) emitComment("-> Id") ;
      loc = st_lookup(tree->attr.name);
      emitRM("LD",ac,loc,gp,"load id value");
      if (TraceCode)  emitComment("<- Id") ;
      break; /* IdK */

    case OpK :
         if (TraceCode) emitComment("-> Op") ;
         p1 = tree->child[0];
         p2 = tree->child[1];
         /* gen code for ac = left arg */
         cGen(p1);
         /* gen code to push left operand */
         emitRM("ST",ac,tmpOffset--,mp,"op: push left");
         /* gen code for ac = right operand */
         cGen(p2);
         /* now load left operand */
         emitRM("LD",ac1,++tmpOffset,mp,"op: load left");
         switch (tree->attr.op) {
            case PLUS :
               emitRO("ADD",ac,ac1,ac,"op +");
               break;
            case MINUS :
               emitRO("SUB",ac,ac1,ac,"op -");
               break;
            case TIMES :
               emitRO("MUL",ac,ac1,ac,"op *");
               break;
            case OVER :
               emitRO("DIV",ac,ac1,ac,"op /");
               break;
            case LT :
               emitRO("SUB",ac,ac1,ac,"op <") ;
               emitRM("JLT",ac,2,pc,"br if true") ;
               emitRM("LDC",ac,0,ac,"false case") ;
               emitRM("LDA",pc,1,pc,"unconditional jmp") ;
               emitRM("LDC",ac,1,ac,"true case") ;
               break;
            case EQ :
               emitRO("SUB",ac,ac1,ac,"op ==") ;
               emitRM("JEQ",ac,2,pc,"br if true");
               emitRM("LDC",ac,0,ac,"false case") ;
               emitRM("LDA",pc,1,pc,"unconditional jmp") ;
               emitRM("LDC",ac,1,ac,"true case") ;
               break;
            default:
               emitComment("BUG: Unknown operator");
               break;
         } /* case op */
         if (TraceCode)  emitComment("<- Op") ;
         break; /* OpK */

    default:
      break;
  }
} /* genExp */

/* Procedure cGen recursively generates code by
 * tree traversal
 */
static void cGen( TreeNode * tree)
{ if (tree != NULL)
  { switch (tree->nodekind) {
      case StmtK:
        genStmt(tree);
        break;
      case ExpK:
        genExp(tree);
        break;
      default:
        break;
    }
    cGen(tree->sibling);
  }
}

int getSizeOfGlobal(TreeNode * syntaxTree)
{
   int result = 0;
   TreeNode *tree = syntaxTree;
   while(tree != NULL)
   {
     if(tree->nodekind == ExpK && tree->kind.stmt == VarArrayK)
       result += tree->child[0]->attr.val;
     else
       result++;
     tree = tree->sibling;
   }
   return result;
}

/**********************************************/
/* the primary function of the code generator */
/**********************************************/
/* Procedure codeGen generates code to a code
 * file by traversal of the syntax tree. The
 * second parameter (codefile) is the file name
 * of the code file, and is used to print the
 * file name as a comment in the code file
 */
void codeGen(TreeNode * syntaxTree, char * codefile)
{  char * s = malloc(strlen(codefile)+7);
   strcpy(s,"File: ");
   strcat(s,codefile);
   emitComment("TINY Compilation to TM Code");
   emitComment(s);
   /* generate standard prelude */
   emitComment("Standard prelude:");
   emitRM("LD",mp,0,ac,"load maxaddress from location 0");
   emitRM("ST",ac,0,ac,"clear location 0");
   emitComment("End of standard prelude.");
   /* generate code for TINY program */
   forFunctionTable = emitSkip(getSizeOfGlobal(syntaxTree)*2 + 1);
   cGen(syntaxTree);
   /* jump to main */
   int memloc = st_get_location("~", "main");
   emitBackup(forFunctionTable);
   emitRM("LDC", pc, locMain, 0, "jump to main");
   emitRestore();
   /* finish */
   emitComment("End of execution.");
   emitRO("HALT",0,0,0,"done");
}

int pushArguments(int depth, TreeNode * tree)
{
   if(tree == NULL)
    return depth;
   depth = pushArguments(depth + 1, tree->sibling);
   genExp(tree);
   //parameterStack[--parameterStackIndex] = tree->attr.name;
   //emitRM("LDC", ac1, 1, 0, "ac1 = 1");
   //emitRO("SUB", mp, mp, ac1, "mp = mp - ac1");
   emitRM("ST", ac, --tmpOffset, mp, "op: push argument(reverse order)");
   return depth;
}

void pushParameters(char *functionName)
{
   char* parameters[SIZE];
   char tmp[128];
   int max = 0;
   sprintf(tmp, "~:%s", functionName);
   ScopeList scope = getScope(tmp);
   if(scope == NULL)
     return;
   int i;
   for (i=0;i<SIZE;++i)
   { if (scope->bucket[i] != NULL)
     { BucketList l = scope->bucket[i];
       while (l != NULL)
       {
         if(max < l->memloc)
           max = l->memloc;
         parameters[l->memloc] = l->name;
         l = l->next;
       }
     }
   }

   for (i=max; i>=0;i--)
     parameterStack[--parameterStackIndex] = parameters[i];
}

void insertFunction(int functionLocation, char *name)
{
   char comment[128];
   int memloc = st_get_location("~", name);
   emitBackup(forFunctionTable);
   forFunctionTable += 2;
   sprintf(comment, "function %s is at %d", name, memloc);
   if (TraceCode) emitComment(comment);
   sprintf(comment, "load function location(%d)", functionLocation);
   emitRM("LDC", ac, functionLocation, 0, comment);
   emitRM("ST", ac, memloc, gp, "add into memory");
   emitRestore();
}

int getLocalNameOffset(char *name)
{
   int i;
   for(i = localNameStackIndex; i < 1024; i++)
     if(localNameStack[i] != 0 && strcmp(localNameStack[i], name) == 0)
       return i - localNameStackIndex;
   return -1;
}

int getParameterOffset(char *name)
{
   int i;
   for(i = parameterStackIndex; i< 1024; i++)
     if(parameterStack[i] != 0 && strcmp(parameterStack[i], name) == 0)
       return i - parameterStackIndex;
   return -1;
}