/*--------------------------------------------------------------------------*/
/*                                                                          */
/*       parser2.c      Extends parser1.c Now implements an                 */
/*                      Augmented S-Algol Syntax error recovery system      */
/*                      Which allows the parser to continue parsing after   */
/*                      it reports an error                                 */
/*                                                                          */
/*                                                                          */
/*       This parser will follow the CPL grammar found in the 'grammar.pdf  */
/*       file on Sulis. Program prints "valid" to terminal if source code   */
/*       is syntactically valid; prints "syntax error" otherwise.           */
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

PRIVATE TOKEN  CurrentToken;       /*  Parser lookahead token.  Updated by  */
                                   /*  routine Accept (below).  Must be     */
                                   /*  initialised before parser starts.    */

/*---------------------------------------------------------------------------

	Declare Sets

----------------------------------------------------------------------------*/

SET StatementFS_aug, StatementFBS, ProgProcDecSet1, ProgProcDecSet2;
SET BlockSet1;
SET FB_Prog, FB_ProcDec, FB_Block;
SET StatementFS_aug, StatementFBS;

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
PRIVATE void ParseRestOfStatement( void );
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
PRIVATE void ParseBooleanExpression( void );
PRIVATE void ParseAddOp( void );
PRIVATE void ParseMultOp( void );
PRIVATE void ParseRelOp( void );
PRIVATE void SetupSets( void );
PRIVATE void Synchronise( SET *F, SET*FB );

PRIVATE int  OpenFiles( int argc, char *argv[] );
PRIVATE void Accept( int code );
PRIVATE void ReadToEndOfFile( void );



/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  Main: Program entry point.  Sets up parser globals (opens input and     */
/*        output files, initialises current lookahead), then calls          */
/*        "ParseProgram" to start the parse.                                */
/*                                                                          */
/*--------------------------------------------------------------------------*/

PUBLIC int main ( int argc, char *argv[] )
{
    if ( OpenFiles( argc, argv ) )  
    {
		InitCharProcessor( InputFile, ListFile );
		CurrentToken = GetToken();
		SetupSets();
		ParseProgram();
		ReadToEndOfFile();
		fclose( InputFile );
		fclose( ListFile );
		return  EXIT_SUCCESS;
	}
	
	else 
		return EXIT_FAILURE;
}

/* ----------------------------------------------------------------------  */
/*                                                                         */
/* SetupSets: This function serves the purpose of initializing all         */
/* the sets used for the Primary Augmented S-Algol Error recovery          */
/* and resync                                                              */
/*                                                                         */
/* Inputs: None                                                            */
/*                                                                         */
/* Outputs: None                                                           */
/*                                                                         */
/* Returns: Nothing                                                        */
/*                                                                         */
/* ----------------------------------------------------------------------  */

PRIVATE void SetupSets( void )
{
	InitSet(&StatementFS_aug, 6, IDENTIFIER, WHILE, IF, READ, WRITE, END );
	InitSet(&StatementFBS, 4, SEMICOLON, ELSE, ENDOFPROGRAM, ENDOFINPUT );
	InitSet(&ProgProcDecSet1, 3, VAR, PROCEDURE, BEGIN );
	InitSet(&ProgProcDecSet2, 2, PROCEDURE, BEGIN );
	InitSet(&BlockSet1, 6, IDENTIFIER, WHILE, IF, READ, WRITE, END);
	
	InitSet(&FB_Prog, 3, ENDOFPROGRAM, ENDOFINPUT, END);
	InitSet(&FB_ProcDec, 3, ENDOFPROGRAM, ENDOFINPUT, END);
	InitSet(&FB_Block, 4, ENDOFINPUT, ELSE, SEMICOLON, ENDOFPROGRAM );
 
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
/*    Side Effects: Lookahead token advanced.                               */
/*                                                                          */
/*--------------------------------------------------------------------------*/

PRIVATE void Synchronise(SET *F, SET *FB)
{
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
/*    Side Effects: Lookahead token advanced.                               */
/*                                                                          */
/*--------------------------------------------------------------------------*/

PRIVATE void ParseProgram( void )
{
    Accept( PROGRAM );  
    Accept( IDENTIFIER );
    Accept( SEMICOLON );

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
    Accept( ENDOFPROGRAM );     /* Token "." has name ENDOFPROGRAM          */
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
/*    Side Effects: Lookahead token advanced.                               */
/*                                                                          */
/*--------------------------------------------------------------------------*/

PRIVATE void ParseDeclarations( void )
{
    Accept( VAR );
    Accept( IDENTIFIER );

    while ( CurrentToken.code == COMMA ) {
        Accept( COMMA );
        Accept( IDENTIFIER );
    }
    Accept( SEMICOLON );
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
/*    Side Effects: Lookahead token advanced.                               */
/*                                                                          */
/*--------------------------------------------------------------------------*/

PRIVATE void ParseProcDeclaration( void )
{
    Accept( PROCEDURE );
    Accept( IDENTIFIER );

    if ( CurrentToken.code == LEFTPARENTHESIS ) {
    	ParseParameterList();
    }
    Accept( SEMICOLON );
    
    Synchronise( &ProgProcDecSet1, &FB_ProcDec ); 
    if ( CurrentToken.code == VAR ) {
    	ParseDeclarations();
    }
    Synchronise( &ProgProcDecSet2, &FB_ProcDec );
    while ( CurrentToken.code == PROCEDURE )  ParseProcDeclaration();
    Synchronise( &ProgProcDecSet2, &FB_ProcDec );    
    ParseBlock();
    
    Accept( SEMICOLON );
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
/*    Side Effects: Lookahead token advanced.                               */
/*                                                                          */
/*--------------------------------------------------------------------------*/

PRIVATE void ParseParameterList( void )
{
	Accept( LEFTPARENTHESIS );
	ParseFormalParameter();
	
	while ( CurrentToken.code == COMMA ) {
		Accept( COMMA );
		ParseFormalParameter();
	}
	
	Accept( RIGHTPARENTHESIS );
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
/*    Side Effects: Lookahead token advanced.                               */
/*                                                                          */
/*--------------------------------------------------------------------------*/

PRIVATE void ParseFormalParameter( void )
{
    if ( CurrentToken.code == REF ) Accept( REF );
    
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

PRIVATE void ParseBlock( void )
{
	int token;

	Accept( BEGIN );
	Synchronise( &StatementFS_aug, &StatementFBS );
	Synchronise( &BlockSet1, &FB_Block );

    while ( (token = CurrentToken.code) == IDENTIFIER || token == WHILE ||
    		 token == IF || token == READ || token == WRITE)  {
        ParseStatement();
        Accept( SEMICOLON );
        Synchronise( &StatementFS_aug, &StatementFBS );
        Synchronise( &BlockSet1, &FB_Block );
    }

    Accept( END );
}


/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  ParseStatement implements:                                        */
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
	Accept( IDENTIFIER );
	ParseRestOfStatement();
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

PRIVATE void ParseRestOfStatement( void )
{
	if ( CurrentToken.code == LEFTPARENTHESIS ) ParseProcCallList();
	
	else if ( CurrentToken.code == ASSIGNMENT ) ParseAssignment();
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

PRIVATE void ParseAssignment( void )
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

PRIVATE void ParseWhileStatement( void )
{
    Accept( WHILE );
    ParseBooleanExpression();
    Accept( DO );
    ParseBlock();
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

PRIVATE void ParseIfStatement( void )
{
    Accept( IF );
    ParseBooleanExpression();
    Accept( THEN );
    ParseBlock();
    
    if (CurrentToken.code == ELSE )  {
    	Accept( ELSE );
    	ParseBlock();
    }
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

PRIVATE void ParseReadStatement( void )
{
    Accept( READ );
    Accept( LEFTPARENTHESIS );
    Accept( IDENTIFIER );
    
    while (CurrentToken.code == COMMA )  {
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

PRIVATE void ParseWriteStatement( void )
{
    Accept( WRITE );
    Accept( LEFTPARENTHESIS );
    ParseExpression();
    
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

PRIVATE void ParseExpression( void )
{
    int token;

    ParseCompoundTerm();

    while ( (token = CurrentToken.code) == ADD ||    /* ADD: name for "+".  */
            token == SUBTRACT ) {                    /* SUBTRACT: "-".      */
        ParseAddOp();
        ParseCompoundTerm();
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
    /* EBNF "or" operator: "|" implemented as an if-else block. If-path     */
    /* triggered by a <Variable> in the input stream, which is just an      */
    /* <Identifier>, i.e., token IDENTIFIER.  Else-path is taken otherwise. */
    /* N.B., in the case of a syntax-error, this error will be reported as  */
    /* "INTCONST expected" because of this behaviour.                       */

    if ( CurrentToken.code == SUBTRACT )  Accept( SUBTRACT );
    
    ParseSubTerm();
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

PRIVATE void ParseBooleanExpression( void )
{
    /* EBNF "or" operator: "|" implemented as an if-else block. If-path     */
    /* triggered by a <Variable> in the input stream, which is just an      */
    /* <Identifier>, i.e., token IDENTIFIER.  Else-path is taken otherwise. */
    /* N.B., in the case of a syntax-error, this error will be reported as  */
    /* "INTCONST expected" because of this behaviour.                       */

    ParseExpression();
    ParseRelOp();
    ParseExpression();
}


/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  ParseTerm implements:                                                   */
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

PRIVATE void ParseSubTerm( void )
{
    /* EBNF "or" operator: "|" implemented as an if-else block. If-path     */
    /* triggered by a <Variable> in the input stream, which is just an      */
    /* <Identifier>, i.e., token IDENTIFIER.  Else-path is taken otherwise. */
    /* N.B., in the case of a syntax-error, this error will be reported as  */
    /* "INTCONST expected" because of this behaviour.                       */

    /*  Variable */
    if ( CurrentToken.code == IDENTIFIER )  Accept( IDENTIFIER );
    
    /* Int Const */
    else if (CurrentToken.code == INTCONST )  Accept( INTCONST );
    
    /* Expression */
    else {
    Accept( LEFTPARENTHESIS );
    ParseExpression();
    Accept( RIGHTPARENTHESIS );
    }
}


/*--------------------------------------------------------------------------*/
/*                                                                          */
/*  ParseExpression implements:                                             */
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
/*  ParseExpression implements:                                             */
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
/*  ParseExpression implements:                                             */
/*                                                                          */
/*       <RelOp>  :==   "=" | "<=" | ">=" | "<" | ">"                       */
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

PRIVATE void ParseRelOp( void )
{
    if ( CurrentToken.code == EQUALITY ) Accept( EQUALITY );
    
    else if ( CurrentToken.code == LESSEQUAL ) Accept( LESSEQUAL );
    
    else if ( CurrentToken.code == GREATEREQUAL ) Accept( GREATEREQUAL );
    
    else if ( CurrentToken.code == LESS ) Accept( LESS );
    
    else if ( CurrentToken.code == GREATER ) Accept( GREATER );
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
/*    Note that this routine modifies the globals "InputFile" and           */
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

    if ( argc != 3 )  {
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
    if ( CurrentToken.code != ENDOFINPUT )  {
        Error( "Parsing ends here in this program\n", CurrentToken.pos );
        while ( CurrentToken.code != ENDOFINPUT )  CurrentToken = GetToken();
    }
}
