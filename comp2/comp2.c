/*--------------------------------------------------------------------------*/
/*                                                                          */
/*       comp2.c                                                            */
/*       23/04/2021                                                                   */
/*                                                                          */
/*       This is a full compiler performing syntax and semantic error       */
/*       detection and code generation for the CPL language, including      */
/*       procedure definitions.                                             */
/*                                                                          */
/*       Authors: Ronan Keaveney, Charlie Gorey O'Neill,                    */
/*                Conor Cosgrave, Emmett Lawlor                             */
/*                                                                          */
/*                                                                          */
/*--------------------------------------------------------------------------*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "global.h"
#include "scanner.h"
#include "line.h"
#include "code.h"
#include "debug.h"
#include "sets.h"
#include "strtab.h"
#include "symbol.h"

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  Global variables used by this parser.                                   */
/*                                                                          */
/*--------------------------------------------------------------------------*/

PRIVATE FILE *InputFile;           /*  CPL source comes from here.          */
PRIVATE FILE *ListFile;            /*  For nicely-formatted syntax errors.  */
PRIVATE FILE *CodeFile;			   /* machine code output file */

PRIVATE TOKEN  CurrentToken;       /*  Parser lookahead token.  Updated by  */
                                   /*  routine Accept (below).  Must be     */
                                   /*  initialised before parser starts.    */
PRIVATE int Read;
PRIVATE int scope;			   
PRIVATE int Write;                 
PRIVATE int VarLctn;			   
PRIVATE int FlagError; 


/*---------------------------------------------------------------------------

	Declare Sets

----------------------------------------------------------------------------*/

PRIVATE SET StatementFS_aug, StatementFBS, ProgProcDecSet1, ProgProcDecSet2;
PRIVATE SET BlockSet1;
PRIVATE SET FB_Prog, FB_ProcDec, FB_Block;
PRIVATE SET StatementFS_aug2, StatementFBS2;


/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  Function prototypes                                                     */
/*                                                                          */
/*--------------------------------------------------------------------------*/


PRIVATE void ParseProgram( void );
PRIVATE void ParseDeclarations( void );
PRIVATE void ParseProcDeclaration( void );
PRIVATE void ParseParameterList( void );
PRIVATE void ParseFormalParameter( void );
PRIVATE void ParseBlock( void );
PRIVATE void ParseStatement( void );
PRIVATE void ParseSimpleStatement( void );
PRIVATE void ParseRestOfStatement( SYMBOL *target );
PRIVATE void ParseProcCallList( void );
PRIVATE void ParseAssignment( void );
PRIVATE void ParseActualParameter( void );
PRIVATE void ParseWhileStatement( void );
PRIVATE void ParseIfStatement( void );
PRIVATE void ParseReadStatement( void );
PRIVATE void ParseWriteStatement( void );
PRIVATE void ParseExpression( void );
PRIVATE void ParseCompoundTerm( void );
PRIVATE void ParseTerm( void );
PRIVATE void ParseSubTerm( void );
PRIVATE void ParseAddOp( void );
PRIVATE void ParseMultOp( void );
PRIVATE void SetupSets( void );
PRIVATE void Synchronise( SET *F, SET*FB );
PRIVATE void Accept( int code );
PRIVATE void ReadToEndOfFile( void );
PRIVATE void ParseIntConst(void); 
PRIVATE void ParseIdentifier(void);

PRIVATE int ParseBooleanExpression( void );
PRIVATE int ParseRelOp( void );
PRIVATE int OpenFiles( int argc, char *argv[] );

PRIVATE SYMBOL *MakeSymbolTableEntry(int symtype);
PRIVATE SYMBOL *LookupSymbol();


/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  Main: Smallparser entry point.  Sets up parser globals (opens input and */
/*        output files, initialises current lookahead), then calls          */
/*        "ParseProgram" to start the parse.                                */
/*                                                                          */
/*--------------------------------------------------------------------------*/

PUBLIC int main ( int argc, char *argv[] )
{
    Write=0;
    Read = 0;
    scope = 1;
    FlagError=0;
    VarLctn = 0;
    if ( OpenFiles( argc, argv ) )
    {
        InitCharProcessor( InputFile, ListFile );
        InitCodeGenerator(CodeFile); /*Initialize code generation*/
        CurrentToken = GetToken();
        SetupSets();
        ParseProgram();
        WriteCodeFile();  /*Write out assembly to file*/
        fclose( InputFile );
        fclose( ListFile );
        if(FlagError) 
        {
            printf("Syntax Error Detected\n");
            return EXIT_FAILURE;
        }
        printf("Valid, No Errors Detected\n");
        return  EXIT_SUCCESS;
    }
    else
    {
        printf("Syntax Error Dectected\n"); 
        return EXIT_FAILURE;
    }
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/* SetupSets: This function serves the purpose of initializing all         */
/* the sets used for the Primary Augmented S-Algol Error recovery          */
/* and resync                                                              */
/*                                                                         */
/* Inputs: None                                                            */
/*                                                                         */
/* Outputs: None                                                           */
/*                                                                         */
/* Returns: Nothing                                                   */
/*                                                                          */
/*--------------------------------------------------------------------------*/

PRIVATE void SetupSets(void){

 	InitSet(&StatementFS_aug, 6, IDENTIFIER, WHILE, IF, READ, WRITE, END );
	InitSet(&StatementFBS, 4, SEMICOLON, ELSE, ENDOFPROGRAM, ENDOFINPUT );
	InitSet(&ProgProcDecSet1, 3, VAR, PROCEDURE, BEGIN );
	InitSet(&ProgProcDecSet2, 2, PROCEDURE, BEGIN );
	InitSet(&BlockSet1, 6, IDENTIFIER, WHILE, IF, READ, WRITE, END);
	
	InitSet(&FB_Prog, 3, ENDOFPROGRAM, ENDOFINPUT, END);
	InitSet(&FB_ProcDec, 3, ENDOFPROGRAM, ENDOFINPUT, END);
	InitSet(&FB_Block, 4, ENDOFINPUT, ELSE, SEMICOLON, ENDOFPROGRAM );
    InitSet(&StatementFS_aug2, 6, IDENTIFIER, WHILE, IF, READ, WRITE, END );
	InitSet(&StatementFBS2, 4, SEMICOLON, ELSE, ENDOFPROGRAM, ENDOFINPUT );

}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/* Synchronise Function: Once the parser encounters a syntax error          */
/* this will skip over the error and resync, allowing the parser to         */
/* continue parsing the program                                             */
/*                                                                          */
/* Inputs: F = Augmented First Set                                          */
/*         FB = Follow Set union Beacons Set                                */
/*                                                                          */
/* Outputs: S = F union FB                                                  */
/*                                                                          */
/*    Returns:      Nothing                                                 */
/*                                                                          */
/*    Side Effects: Lookahead token advanced.                             */
/*                                                                          */
/*--------------------------------------------------------------------------*/

PRIVATE void Synchronise(SET *F, SET *FB){

    SET S;

   S = Union( 2, F, FB );
	if( !InSet( F, CurrentToken.code ) )
	{
    	SyntaxError2( *F, CurrentToken );
		while( !InSet( &S, CurrentToken.code ) )
			CurrentToken = GetToken();
	}
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  Parser routines: Recursive-descent implementaion of the grammar's       */
/*                   productions.                                           */
/*                                                                          */
/*                                                                          */
/*  ParseProgram implements:                                                */
/*                                                                          */
/*       <Program>  :==  "PROGRAM" <Identifier> ";"                         */
/*                       [ <Declarations> ] {<ProcDeclaration>}             */
/*						 <Block>  "."               */
/*                                                                          */
/*    Sync Points before [<Declarations>]                                   */
/*                before and immediatley after [<ProcDeclarations>]         */
/*                                                                          */
/*    Inputs:       None                                                    */
/*                                                                          */
/*    Outputs:      None                                                    */
/*                                                                          */
/*    Returns:      Nothing                                                 */
/*                                                                          */
/*    Side Effects: Lookahead token advanced.                                                          */
/*                                                                          */
/*--------------------------------------------------------------------------*/



PRIVATE void ParseProgram(void)
{

    Accept(PROGRAM);
    Accept(IDENTIFIER);
    Accept(SEMICOLON);
    
    MakeSymbolTableEntry(STYPE_PROGRAM);

    /* Synchronise ParseProgramSet1, Followers, Beacons */
    Synchronise( &ProgProcDecSet1, &FB_Prog );
    
    if ( CurrentToken.code == VAR )  ParseDeclarations();
    	/* Synchronise ParseProgramSet2, Followers, Beacons */
    	Synchronise( &ProgProcDecSet2, &FB_Prog );
    
    /*  Recursive ProcDeclaration                       */
    while (CurrentToken.code == PROCEDURE )
    {
		ParseProcDeclaration();
    	/* Synchronise ParseProgramSet2, Followers, Beacons */
		Synchronise( &ProgProcDecSet2, &FB_Prog );
    }
    
    ParseBlock();
    Accept( ENDOFPROGRAM );
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  ParseDeclararions implements:                                           */
/*                                                                          */
/*       <Declarations>  :==  "VAR" <Variable> { "," <Variable> } ";"       */
/*                                                                          */
/*                                                                          */
/*    Inputs:       None                                                    */
/*                                                                          */
/*    Outputs:      None                                                    */
/*                                                                          */
/*    Returns:      Nothing                                                 */
/*                                                                          */
/*    Side Effects: Lookahead token advanced.                                                 */
/*                                                                          */
/*--------------------------------------------------------------------------*/

PRIVATE int ParseDeclarations(void)
{
    int VarCounter = 0;
    Accept( VAR );
    MakeSymbolTableEntry(STYPE_VARIABLE);
    Accept( IDENTIFIER );

    ParseVariable();
    VarCounter++;
    while (CurrentToken.code == COMMA)
    {
        Accept(COMMA);
        MakeSymbolTableEntry(STYPE_VARIABLE);
        Accept( IDENTIFIER );
        ParseVariable();
        VarCounter++;
    }
    
    Accept(SEMICOLON);

return VarCounter; 
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  ParseProcDeclararion implements:                                        */
/*                                                                          */
/*       <ProcDeclaration>  :==  "PROCEDURE" <Identifier>                   */
/*			      	 [<ParameterList>] ";" [<Declarations>]     */
/*			     	 {<ProcDeclaration>} <Block> ";"            */
/*                                                                          */
/*    Sync Points before [<Declarations>]                                   */
/*                before and immediatley after [<ProcDeclarations>]         */
/*                                                                          */
/*    Inputs:       None                                                    */
/*                                                                          */
/*    Outputs:      None                                                    */
/*                                                                          */
/*    Returns:      Nothing                                                 */
/*                                                                          */
/*    Side Effects: Lookahead token advanced.                                                */
/*                                                                          */
/*                                                                          */
/*--------------------------------------------------------------------------*/

PRIVATE void ParseProcDeclaration(void)
{
    int VarCounter = 0;
    int Add;
    SYMBOL *procedure;

    scope++;

   if ( CurrentToken.code == LEFTPARENTHESIS ) 
    {
    	ParseParameterList();
    }
    Accept( SEMICOLON );
    
    Synchronise( &ProgProcDecSet1, &FB_ProcDec ); 
    
    if ( CurrentToken.code == VAR ) 
    {
    	ParseDeclarations();
    }
    
    Synchronise( &ProgProcDecSet2, &FB_ProcDec );
    
    while ( CurrentToken.code == PROCEDURE )  
    	ParseProcDeclaration();
    	
    Synchronise( &ProgProcDecSet2, &FB_ProcDec );    
    ParseBlock();
    
    Accept( SEMICOLON );
    
    RemoveSymbols( scope );
    scope--;
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  ParseParameterList implements:                                          */
/*                                                                          */
/*       <ParameterList>  :==  "(" <FormalParameter> { ","                  */
/*                             <FormalParameter> } ")"                      */
/*                                                                          */
/*    Inputs:       None                                                    */
/*                                                                          */
/*    Outputs:      None                                                    */
/*                                                                          */
/*    Returns:      Nothing                                                 */
/*                                                                          */
/*    Side Effects: Lookahead token advanced.                               */                         */
/*                                                                          */
/*--------------------------------------------------------------------------*/

PRIVATE void ParseParameterList(void)
{
    Accept( LEFTPARENTHESIS );
    ParseFormalParameter();

    while (CurrentToken.code == COMMA)
    {
        Accept(COMMA);
        ParseFormalParameter();
    }
    Accept(RIGHTPARENTHESIS);
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  ParseFormalParameter implements:                                        */
/*                                                                          */
/*       <FormalParameter>  :==  [ "REF" ] <Variable>   */
/*                                                                          */
/*                                                                          */
/*    Inputs:       None                                                    */
/*                                                                          */
/*    Outputs:      None                                                    */
/*                                                                          */
/*    Returns:      Nothing                                                 */
/*                                                                          */
/*    Side Effects: Lookahead token advanced.                          */
/*                                                                          */
/*                                                                          */
/*--------------------------------------------------------------------------*/

PRIVATE void ParseFormalParameter(void)
{
    if(CurrentToken.code == REF){
        MakeSymbolTableEntry(STYPE_REFPAR); 
        Accept(REF);
    }

    MakeSymbolTableEntry(STYPE_VARIABLE);

    ParseVariable(); 
    Accept( IDENTIFIER );
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  ParseBlock implements:                                                  */
/*                                                                          */
/*       <Block>  :==  "BEGIN" { <Statement> ";" } "END"                    */
/*                                                                          */
/*    Sync Points  before and immediatley after [<Statement>]               */
/*                                                                          */
/*    Inputs:       None                                                    */
/*                                                                          */
/*    Outputs:      None                                                    */
/*                                                                          */
/*    Returns:      Nothing                                                 */
/*                                                                          */
/*    Side Effects: Lookahead token advanced.                               */
/*                                                                          */
/*--------------------------------------------------------------------------*/

PRIVATE void ParseBlock(void)
{
Accept(BEGIN);

Synchronise(&StatementFS_aug,&StatementFBS);
Synchronise( &BlockSet1, &FB_Block );

 while (CurrentToken.code == IDENTIFIER || CurrentToken.code == WHILE ||
    CurrentToken.code == IF || CurrentToken.code == READ ||
    CurrentToken.code == WRITE){
    ParseStatement();
    Accept(SEMICOLON);
    
    Synchronise( &StatementFS_aug, &StatementFBS );
    Synchronise( &BlockSet1, &FB_Block );
}


Accept(END);
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  ParseStatement implements:                                              */
/*                                                                          */
/*       <Statement>  :==  <SimpleStatement> | <WhileStatement>  |          */
/*                         <IfStatement> | <ReadStatement> |                */
/*                         <WriteStatement>                                 */
/*                                                                          */
/*                                                                          */
/*    Inputs:       None                                                    */
/*                                                                          */
/*    Outputs:      None                                                    */
/*                                                                          */
/*    Returns:      Nothing                                                 */
/*                                                                          */
/*    Side Effects: Lookahead token advanced.                               */
/*                                                                          */
/*--------------------------------------------------------------------------*/


PRIVATE void ParseStatement( void )
{
	if ( CurrentToken.code == IDENTIFIER ) ParseSimpleStatement();
	
	else if ( CurrentToken.code == WHILE ) ParseWhileStatement();
	
	else if ( CurrentToken.code == IF ) ParseIfStatement();
	
	else if ( CurrentToken.code == READ ) ParseReadStatement();
	
	else if ( CurrentToken.code == WRITE ) ParseWriteStatement();
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  ParseSimpleStatement implements:                                        */
/*                                                                          */
/*       <SimpleStatement>  :==  <VarOrProcName> <RestOfStatement>          */
/*                                                                          */
/*                                                                          */
/*    Inputs:       None                                                    */
/*                                                                          */
/*    Outputs:      None                                                    */
/*                                                                          */
/*    Returns:      Nothing                                                 */
/*                                                                          */
/*    Side Effects: Lookahead token advanced.                               */
/*                                                                          */
/*--------------------------------------------------------------------------*/


PRIVATE void ParseSimpleStatement( void )
{
	SYMBOL *target;
	
	target = LookupSymbol(); 
	Accept( IDENTIFIER );
	ParseRestOfStatement( target );
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  ParseRestOfStatement implements:                                        */
/*                                                                          */
/*       <RestOfStatement>  :==  <ProcCallList> | <Assignment> | EPSILON    */
/*                                                                          */
/*                                                                          */
/*    Inputs:       None                                                    */
/*                                                                          */
/*    Outputs:      None                                                    */
/*                                                                          */
/*    Returns:      Nothing                                                 */
/*                                                                          */
/*    Side Effects: Lookahead token advanced.                               */
/*                                                                          */
/*--------------------------------------------------------------------------*/


PRIVATE void ParseRestOfStatement( SYMBOL *target )
{
	switch ( CurrentToken.code )
	{
		case LEFTPARENTHESIS :
			ParseProcCallList();
			
		case SEMICOLON :
			if ( target != NULL && target->type == STYPE_PROCEDURE )
				Emit ( I_CALL, target->address );
			else
			{
				Error( "Not a procedure\n", CurrentToken.pos );
				KillCodeGeneration();
			}
			break;
		
		case ASSIGNMENT : 
		default :
			ParseAssignment();
			if ( target != NULL && target->type == STYPE_VARIABLE )
				Emit ( I_STOREA, target->address );
			else
			{
				Error( "Undeclared variable\n", CurrentToken.pos );
				KillCodeGeneration();
			}
			break;
	}
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  ParseProcCallList implements:                                           */
/*                                                                          */
/*       <ProcCallList>  :==  "(" <ActualParameter> { ","                   */
/*                            <ActualParameter> } ")"                       */
/*                                                                          */
/*    Inputs:       None                                                    */
/*                                                                          */
/*    Outputs:      None                                                    */
/*                                                                          */
/*    Returns:      Nothing                                                 */
/*                                                                          */
/*    Side Effects: Lookahead token advanced.                               */
/*                                                                          */
/*--------------------------------------------------------------------------*/


PRIVATE void ParseProcCallList( void )
{
	Accept( LEFTPARENTHESIS );
	ParseActualParameter();
	
	while ( CurrentToken.code == COMMA ) {
		Accept( COMMA );
		ParseActualParameter();
	}
	
	Accept( RIGHTPARENTHESIS );
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  ParseAssignment implements:                                             */
/*                                                                          */
/*       <Assignment>  :==  ":=" <Expression>                               */
/*                                                                          */
/*                                                                          */
/*    Inputs:       None                                                    */
/*                                                                          */
/*    Outputs:      None                                                    */
/*                                                                          */
/*    Returns:      Nothing                                                 */
/*                                                                          */
/*    Side Effects: Lookahead token advanced.                               */
/*                                                                          */
/*--------------------------------------------------------------------------*/


PRIVATE void ParseAssignment(void)
{
    Accept(ASSIGNMENT);
    ParseExpression();
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  ParseActualParameter implements:                                        */
/*                                                                          */
/*       <ActualParameter>  :==  <Variable> | <Expression>                  */
/*                                                                          */
/*                                                                          */
/*    Inputs:       None                                                    */
/*                                                                          */
/*    Outputs:      None                                                    */
/*                                                                          */
/*    Returns:      Nothing                                                 */
/*                                                                          */
/*    Side Effects: Lookahead token advanced.                               */
/*                                                                          */
/*--------------------------------------------------------------------------*/

PRIVATE void ParseActualParameter( void )
{
    if ( CurrentToken.code == IDENTIFIER ) Accept( IDENTIFIER );
    
    else ParseExpression();
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  ParseWhileStatement implements:                                         */
/*                                                                          */
/*       <WhileStatement>  :==  "WHILE" <BooleanExpression> "DO" <Block>    */
/*                                                                          */
/*                                                                          */
/*    Inputs:       None                                                    */
/*                                                                          */
/*    Outputs:      None                                                    */
/*                                                                          */
/*    Returns:      Nothing                                                 */
/*                                                                          */
/*    Side Effects: Lookahead token advanced.                               */
/*                                                                          */
/*--------------------------------------------------------------------------*/


PRIVATE void ParseWhileStatement(void)
{
    int Label1, Label2, L2BackPatchLoc;
    Accept( WHILE );
	Label1 = CurrentCodeAddress();
	L2BackPatchLoc = ParseBooleanExpression();
    Accept( DO );
    ParseBlock();
	Emit(I_BR, Label1);
	Label2 = CurrentCodeAddress();
	BackPatch(L2BackPatchLoc, Label2);
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  ParseIfStatement implements:                                            */
/*                                                                          */
/*       <IfStatement>  :==  "IF" <BooleanExpression> "THEN"                */
/*                           <Block> [ "ELSE" <Block> ]                     */
/*                                                                          */
/*                                                                          */
/*    Inputs:       None                                                    */
/*                                                                          */
/*    Outputs:      None                                                    */
/*                                                                          */
/*    Returns:      Nothing                                                 */
/*                                                                          */
/*    Side Effects: Lookahead token advanced.                               */
/*                                                                          */
/*--------------------------------------------------------------------------*/


PRIVATE void ParseIfStatement(void)
{
    int L1BackPatchLoc, L2BackPatchLoc;
    Accept( IF );
    L1BackPatchLoc = ParseBooleanExpression();
    Accept( THEN );
    ParseBlock();
    if ( CurrentToken.code == ELSE )
    {
    	L2BackPatchLoc = CurrentCodeAddress();
    	Emit( I_BR, 999 );	// Branch to TEMP code address, 
    						// to be backpatched later
    	BackPatch( L1BackPatchLoc, CurrentCodeAddress() );
    	Accept( ELSE );
    	ParseBlock();
    	BackPatch( L2BackPatchLoc, CurrentCodeAddress() );
    }
    else 
    	BackPatch( L1BackPatchLoc, CurrentCodeAddress() );
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  ParseReadStatement implements:                                          */
/*                                                                          */
/*       <ReadStatement>  :==   "READ" "(" <Variable> { ","                 */
/*                               <Variable> } ")"                           */
/*                                                                          */
/*                                                                          */
/*    Inputs:       None                                                    */
/*                                                                          */
/*    Outputs:      None                                                    */
/*                                                                          */
/*    Returns:      Nothing                                                 */
/*                                                                          */
/*    Side Effects: Lookahead token advanced.                               */
/*                                                                          */
/*--------------------------------------------------------------------------*/


PRIVATE void ParseReadStatement(void)
{
    Accept(READ);
    Accept( LEFTPARENTHESIS );
    Accept( IDENTIFIER );

    Read = 1;
    ParseProcCallList();
    Read = 0;

    while (CurrentToken.code == COMMA )  
    {
    	Accept( COMMA );
    	Accept( IDENTIFIER );
    }
    
    Accept( RIGHTPARENTHESIS );
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  ParseWriteStatement implements:                                         */
/*                                                                          */
/*       <WriteStatement>  :==   "WRITE" "(" <Expression> { ","             */
/*                               <Expression> } ")"                         */
/*                                                                          */
/*                                                                          */
/*    Inputs:       None                                                    */
/*                                                                          */
/*    Outputs:      None                                                    */
/*                                                                          */
/*    Returns:      Nothing                                                 */
/*                                                                          */
/*    Side Effects: Lookahead token advanced.                               */
/*                                                                          */
/*--------------------------------------------------------------------------*/


PRIVATE void ParseWriteStatement(void)
{
    Accept( WRITE );
    Accept( LEFTPARENTHESIS );
    ParseExpression();
    
    Write = 1;
    ParseProcCallList();

    while (CurrentToken.code == COMMA )  {
    	Accept( COMMA );
    	ParseExpression();
    }
    
    Accept( RIGHTPARENTHESIS );

}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  ParseExpression implements:                                             */
/*                                                                          */
/*       <Expression>  :==   <CompoundTerm> { <AddOp> <CompoundTerm> }      */
/*                                                                          */
/*                                                                          */
/*    Inputs:       None                                                    */
/*                                                                          */
/*    Outputs:      None                                                    */
/*                                                                          */
/*    Returns:      Nothing                                                 */
/*                                                                          */
/*    Side Effects: Lookahead token advanced.                               */
/*                                                                          */
/*--------------------------------------------------------------------------*/


PRIVATE void ParseExpression(void)
{
    int op;

    ParseCompoundTerm();
    while ( (op = CurrentToken.code) == ADD ||		/* ADD: name for "+".  */
			op == SUBTRACT )						/* SUBTRACT: "-".      */
    {
        ParseAddOp();
        ParseCompoundTerm();

        if(op == ADD) _Emit(I_ADD); else _Emit(I_SUB);
    }
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  ParseCompoundTerm implements:                                           */
/*                                                                          */
/*       <CompoundTerm>  :==   <Term> { <MultOp> <Term> }                   */
/*                                                                          */
/*                                                                          */
/*    Inputs:       None                                                    */
/*                                                                          */
/*    Outputs:      None                                                    */
/*                                                                          */
/*    Returns:      Nothing                                                 */
/*                                                                          */
/*    Side Effects: Lookahead token advanced.                               */
/*                                                                          */
/*--------------------------------------------------------------------------*/


PRIVATE void ParseCompoundTerm( void )
{
	int token;

    ParseTerm();
    
    while ( (token = CurrentToken.code) == MULTIPLY ||
            token == DIVIDE ) {
        ParseMultOp();
        ParseTerm();

		if(token == MULTIPLY) _Emit( I_MULT ); 
		else _Emit( I_DIV );
    }
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  ParseTerm implements:                                                   */
/*                                                                          */
/*       <Term>  :==   ["-"] <SubTerm>                                      */
/*                                                                          */
/*                                                                          */
/*    Inputs:       None                                                    */
/*                                                                          */
/*    Outputs:      None                                                    */
/*                                                                          */
/*    Returns:      Nothing                                                 */
/*                                                                          */
/*    Side Effects: Lookahead token advanced.                               */
/*                                                                          */
/*--------------------------------------------------------------------------*/

PRIVATE void ParseTerm( void )
{
	int negateflag = 0;

    if ( CurrentToken.code == SUBTRACT ) {
		negateflag = 1;
		Accept( SUBTRACT );
	}
    
    ParseSubTerm();

	if(negateflag) _Emit(I_NEG);
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  ParseTerm implements:                                                   */
/*                                                                          */
/*       <BooleanExpression>  :==   <Expression> <RelOp> <Expression>       */
/*                                                                          */
/*                                                                          */
/*    Inputs:       None                                                    */
/*                                                                          */
/*    Outputs:      None                                                    */
/*                                                                          */
/*    Returns:      Nothing                                                 */
/*                                                                          */
/*    Side Effects: Lookahead token advanced.                               */
/*                                                                          */
/*--------------------------------------------------------------------------*/


PRIVATE int ParseBooleanExpression(void)
{
    int BackPatchAddr, RelOpInstruction;
    ParseExpression();
    RelOpInstruction = ParseRelOp();
    ParseExpression();
    _Emit(I_SUB);
    ParseRelOp();
    BackPatchAddr = CurrentCodeAddress( );
    Emit( RelOpInstruction, 0 );   // Branch to TEMP code address, 
    								// to be backpatched later
    
    return BackPatchAddr;
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  ParseSubTerm implements:                                                */
/*                                                                          */
/*       <SubTerm>  :==   <Variable> | <IntConst> | "(" <Expression> ")"    */
/*                                                                          */
/*                                                                          */
/*                                                                          */
/*    Inputs:       None                                                    */
/*                                                                          */
/*    Outputs:      None                                                    */
/*                                                                          */
/*    Returns:      Nothing                                                 */
/*                                                                          */
/*    Side Effects: Lookahead token advanced.                               */
/*                                                                          */
/*--------------------------------------------------------------------------*/


PRIVATE void ParseSubTerm(void)
{
    int i, j;

    SYMBOL *var;
    switch(CurrentToken.code)
    {
        case INTCONST:
            Emit(I_LOADI,CurrentToken.value);
            ParseIntConst(); 
            break;
        case LEFTPARENTHESIS:
            Accept(LEFTPARENTHESIS);
            ParseExpression();
            Accept(RIGHTPARENTHESIS);
            break;
        case IDENTIFIER:
        default:
            var = LookupSymbol();
            if(var != NULL && var->type == STYPE_VARIABLE) {
                if (Write) {
                    Emit(I_LOADA,var->address); 
                }
                else if (Read) {
                    Emit(I_STOREA,var->address); 
                }
                else {
                    Emit(I_LOADA,var->address);
                }
            }
            else if ( var->type == STYPE_LOCALVAR ) {
            j = scope - var->scope;
            if ( j == 0 )
            Emit( I_LOADFP, var->address );
            else {
            _Emit( I_LOADFP );

            for ( i = 0; i < j - 1; i++ )
                 _Emit( I_LOADSP );
            Emit( I_LOADSP, var->address );
            }
           }

            else printf("Undeclared Name or Variable");
            ParseVariable(); 
            break;
    }
}


/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  ParseAddOp implements:                                             */
/*                                                                          */
/*       <AddOp>  :==   "+" | "-"      */
/*                                                                          */
/*                                                                          */
/*    Inputs:       None                                                    */
/*                                                                          */
/*    Outputs:      None                                                    */
/*                                                                          */
/*    Returns:      Nothing                                                 */
/*                                                                          */
/*    Side Effects: Lookahead token advanced.                               */
/*                                                                          */
/*--------------------------------------------------------------------------*/


PRIVATE void ParseAddOp( void )
{
    if ( CurrentToken.code == ADD ) Accept( ADD );
    
    else Accept ( SUBTRACT );
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  ParseMultOp implements:                                                 */
/*                                                                          */
/*       <MultOp>  :==   "*" | "/"                                          */
/*                                                                          */
/*                                                                          */
/*    Inputs:       None                                                    */
/*                                                                          */
/*    Outputs:      None                                                    */
/*                                                                          */
/*    Returns:      Nothing                                                 */
/*                                                                          */
/*    Side Effects: Lookahead token advanced.                               */
/*                                                                          */
/*--------------------------------------------------------------------------*/


PRIVATE void ParseMultOp( void )
{
    if ( CurrentToken.code == MULTIPLY ) Accept( MULTIPLY );
    
    else Accept ( DIVIDE );
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  ParseRelOp implements:                                                  */
/*                                                                          */
/*       <RelOp>  :==   "=" | "<=" | ">=" | "<" | ">"                       */
/*                                                                          */
/*                                                                          */
/*    Inputs:       None                                                    */
/*                                                                          */
/*    Outputs:      None                                                    */
/*                                                                          */
/*    Returns:      Int RelOpInstruction                                    */
/*                                                                          */
/*    Side Effects: Lookahead token advanced.                               */
/*                                                                          */
/*--------------------------------------------------------------------------*/


PRIVATE int ParseRelOp( void )
{
	int RelOpInstruction;
	switch(CurrentToken.code) {
		case LESSEQUAL:
			RelOpInstruction = I_BG; 
			Accept(LESSEQUAL);
			break;
		case GREATEREQUAL:
			RelOpInstruction = I_BL; 
			Accept(GREATEREQUAL);
			break;
		case LESS:
			RelOpInstruction = I_BGZ; 
			Accept(LESS);
			break;
		case EQUALITY:
			RelOpInstruction = I_BZ; 
			Accept(EQUALITY);
			break;
		case GREATER:
			RelOpInstruction = I_BLZ; 
			Accept(GREATER);
			break;
	}
	return RelOpInstruction;
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  End of parser.  Support routines follow.                                */
/*                                                                          */
/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  Accept:  Takes an expected token name as argument, and if the current   */
/*           lookahead matches this, advances the lookahead and returns.    */
/*                                                                          */
/*           If the expected token fails to match the current lookahead,    */
/*           this routine reports a syntax error and exits ("crash & burn"  */
/*           parsing).  Note the use of routine "SyntaxError"               */
/*           (from "scanner.h") which puts the error message on the         */
/*           standard output and on the listing file, and the helper        */
/*           "ReadToEndOfFile" which just ensures that the listing file is  */
/*           completely generated.                                          */
/*                                                                          */
/*                                                                          */
/*    Inputs:       Integer code of expected token                          */
/*                                                                          */
/*    Outputs:      None                                                    */
/*                                                                          */
/*    Returns:      Nothing                                                 */
/*                                                                          */
/*    Side Effects: If successful, advances the current lookahead token     */
/*                  "CurrentToken".                                         */
/*                                                                          */
/*--------------------------------------------------------------------------*/

PRIVATE void Accept( int ExpectedToken )
{
	static int recovering = 0;
  
	/* Error re-sync code */
	if( recovering )
	{            
    	while( CurrentToken.code != ExpectedToken &&
    		   CurrentToken.code != ENDOFINPUT )
    		CurrentToken = GetToken();
    	recovering = 0;
	}

	/* Normal Accept code */
	if( CurrentToken.code != ExpectedToken )
	{
		SyntaxError( ExpectedToken, CurrentToken );
		recovering = 1;
	}  
	else CurrentToken = GetToken();
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  OpenFiles:  Reads strings from the command-line and opens the           */
/*              associated input and listing files.                         */
/*                                                                          */
/*    Note that this routine mmodifies the globals "InputFile" and          */
/*    "ListingFile".  It returns 1 ("true" in C-speak) if the input and     */
/*    listing files are successfully opened, 0 if not, allowing the caller  */
/*    to make a graceful exit if the opening process failed.                */
/*                                                                          */
/*                                                                          */
/*    Inputs:       1) Integer argument count (standard C "argc").          */
/*                  2) Array of pointers to C-strings containing arguments  */
/*                  (standard C "argv").                                    */
/*                                                                          */
/*    Outputs:      No direct outputs, but note side effects.               */
/*                                                                          */
/*    Returns:      Boolean success flag (i.e., an "int":  1 or 0)          */
/*                                                                          */
/*    Side Effects: If successful, modifies globals "InputFile" and         */
/*                  "ListingFile".                                          */
/*                                                                          */
/*--------------------------------------------------------------------------*/

PRIVATE int  OpenFiles( int argc, char *argv[] )
{


    if ( argc != 4 )  {
        fprintf( stderr, "%s <inputfile> <listfile>\n", argv[0] );
        return 0;
    }

    if ( NULL == ( InputFile = fopen( argv[1], "r" ) ) )  {
        fprintf( stderr, "cannot open \"%s\" for input\n", argv[1] );
        return 0;
    }

    if ( NULL == ( ListFile = fopen( argv[2], "w" ) ) )  {
        fprintf( stderr, "cannot open \"%s\" for output\n", argv[2] );
        fclose( InputFile );
        return 0;
    }

    if ( NULL == ( CodeFile = fopen( argv[3], "w" ) ) )  {
        fprintf( stderr, "cannot open \"%s\" for output\n", argv[3] );
        fclose( InputFile );
        fclose( ListFile );
        return 0;
    }

    return 1;
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  ReadToEndOfFile:  Reads all remaining tokens from the input file.       */
/*              associated input and listing files.                         */
/*                                                                          */
/*    This is used to ensure that the listing file refects the entire       */
/*    input, even after a syntax error (because of crash & burn parsing,    */
/*    if a routine like this is not used, the listing file will not be      */
/*    complete.  Note that this routine also reports in the listing file    */
/*    exactly where the parsing stopped.  Note that this routine is         */
/*    superfluous in a parser that performs error-recovery.                 */
/*                                                                          */
/*                                                                          */
/*    Inputs:       None                                                    */
/*                                                                          */
/*    Outputs:      None                                                    */
/*                                                                          */
/*    Returns:      Nothing                                                 */
/*                                                                          */
/*    Side Effects: Reads all remaining tokens from the input.  There won't */
/*                  be any more available input after this routine returns. */
/*                                                                          */
/*--------------------------------------------------------------------------*/


PRIVATE void ReadToEndOfFile( void )
{
    if ( CurrentToken.code != ENDOFINPUT )  
    {
        Error( "Parsing ends here in this program\n", CurrentToken.pos );
        while ( CurrentToken.code != ENDOFINPUT )  CurrentToken = GetToken();
    }
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  ParseVariable implements:                                               */
/*                                                                          */
/*       <Variable> :== <Identifier>                                        */
/*                                                                          */
/*--------------------------------------------------------------------------*/


PRIVATE void ParseVariable(void)
{
    ParseIdentifier();
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  ParseIntConst implements:                                               */
/*                                                                          */
/*       <IntConst> :== <Digit> { <Digit> }                                 */
/*                                                                          */
/*--------------------------------------------------------------------------*/


PRIVATE void ParseIntConst(void)
{
    Accept(INTCONST);
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  ParseIdentifier implements:                                             */
/*                                                                          */
/*       <Identifier> :== <Alpha> { <AlphaNum> }                            */
/*                                                                          */
/*--------------------------------------------------------------------------*/


PRIVATE void ParseIdentifier(void)
{
    Accept(IDENTIFIER);
}


/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  LookupSymbol:  Gets 's' field from item in lookahead                    */
/*              (i.e.: CurrentToken.s), and then searches for               */
/*               a SYMBOL in Symbol Table which has this name.              */
/*                                                                          */
/*                                                                          */
/*                                                                          */
/*                                                                          */
/*    Inputs:       None                                                    */
/*                                                                          */
/*    Outputs:      None                                                    */
/*                                                                          */
/*    Returns:      Stack Pointer variable, sptr                            */
/*                                                                          */
/*                                                                          */
/*--------------------------------------------------------------------------*/


PRIVATE SYMBOL *LookupSymbol ( void )
{
	SYMBOL *sptr;
	
	if ( CurrentToken.code == IDENTIFIER )
	{
		sptr = Probe ( CurrentToken.s, NULL );
		if ( sptr == NULL )
		{
			Error ( "Identifier not declared", CurrentToken.pos );
			KillCodeGeneration();
		}
	}
	else sptr = NULL;
	
	return sptr;
}


/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  MakeSymbolTableEntry:   Creates symbol table entry for either a         */
/*                          program, variable or procedure                  */
/*                                                                          */
/*                                                                          */
/*                                                                          */
/*                                                                          */
/*                                                                          */
/*    Inputs:       symtype                                                 */
/*                                                                          */
/*    Outputs:      None                                                    */
/*                                                                          */
/*    Returns:      None                                                    */
/*                                                                          */
/*                                                                          */
/*--------------------------------------------------------------------------*/


PRIVATE void MakeSymbolTableEntry ( int symtype )
{
	SYMBOL *oldsptr, *newsptr;
	char *cptr;
	int hashindex;
    static int VarLctn = 0;
	
	if ( CurrentToken.code == IDENTIFIER )
	{
		if ( NULL == ( oldsptr = Probe ( CurrentToken.s, &hashindex )) 
			 ||  oldsptr -> scope < scope )
		{
		 	if ( oldsptr == NULL )
		 		cptr = CurrentToken.s;
		 	else 
		 		cptr = oldsptr -> s;
		 	
		 	if ( NULL == ( newsptr = EnterSymbol ( cptr, hashindex )))
		 	{
		 		Error("Fatal error in EnterSymbol", CurrentToken.pos );
		 		printf("Fatal error in EnterSymbol. ");
		 		printf("Compiler must exit immediately\n");
		 		exit(EXIT_FAILURE);
		 	}
		 	else
			{
				if ( oldsptr == NULL )
					PreserveString();
				
				newsptr -> scope = scope;
				newsptr -> type = symtype;
				
				if ( symtype == STYPE_VARIABLE )
				{
					newsptr -> address = VarLctn;
					VarLctn++;
				}
				else 
					newsptr -> address = -1;
			}
		}
		
		else
		{
			Error("Error! Variable already declared", CurrentToken.pos );
			KillCodeGeneration();
		}	
	}
}



