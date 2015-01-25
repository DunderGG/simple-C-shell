/* 
 * Submit the entire lab1 folder as a tar archive (.tgz).
 * Command to create submission archive: 
      $> tar cvf lab1.tgz lab1/
 */

/*
* 	AUTHOR:    David Bennehag    (David.Bennehag@Gmail.com)
* 	VERSION:    2.0
*
*   DISTRIBUTED SYSTEMS (MPCSN, Chalmers University of Technology)
*
*   Changes from V1.0:
*    * The distribution of messages is now centralized. All nodes send their updates
*      to an elected leader who will broadcast the updates on their behalf.
*
*    * Locks have been implemented to ensure safety of critical sections (mutual exclusion)
*      
*    * Made the info-reporting optional with the flag mycontext['reporting']
*
*    * Removed pagecounts 
*
*	TODO: 	handle pipes
*/

#include <stdio.h>
#include <stdlib.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "parse.h"
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <setjmp.h>
#include <pwd.h>
/*
 * Function declarations
 */

void PrintCommand(int, Command *);
void PrintPgm(Pgm *);
void stripwhite(char *);
void cleanUpChild(int signal_num);
void myBash(Command *cmd);

/* When non-zero, this global means the user is done using this program. */
int done = 0;

pid_t newPid; //The process ID given to the forked child

int bgFlag = 0;

sig_atomic_t child_exit_status;

sigjmp_buf jumpBuffer;

void cleanUpChild(int signal_num)
{
	/* remove child process */
	int status;
	wait(&status);
	
	/* save exit status in a global variable */
	child_exit_status = status;
}

void handle_breaksignal(int signo)
{
	//If we kill process ID 0, we will kill the shell (?), WE DON'T WANT THAT
	if(newPid == 0 && (!bgFlag))
	{
		//Don't use printf(), output functions are not considered safe in signal handlers
		//Use write() instead.
		write(1, "\n", 1);
		
		//Jump back to before the while-loop
		siglongjmp(jumpBuffer, 20001);
	}
	else
	{
		//KILL IT WITH FIRE
		killpg(newPid, SIGTERM);
		
		//Don't use printf(), output functions are not considered safe in signal handlers
		//Use write() instead.
		write(1, "\n", 1);
				
		
		siglongjmp(jumpBuffer, 20001);
	}
}

//Get the current user so we can print it out in the shell
char* userString()
{
	//Read: man getpwuid
	struct passwd *passwd;

	//Get the uid of the running process and use it to get a record from /etc/passwd
	passwd = getpwuid ( getuid() );
	
	return passwd->pw_name;
}
//Just prints the prompt string
void print_WD()
{
	char cwd[1024];
	printf("%s @ %s", userString(), getcwd(cwd, sizeof(cwd)));
	
	
}
//Changes the current working directory based on what was given
char* change_WD(char** pl)
{
	if(*(pl+1) != NULL)
	{
	  
	  if( chdir(*(pl+1)) == -1)
	  {perror("You messed up");}
	  else
	  {printf("changing directory...\n"); print_WD();}
	  
	}
}

//Simple function for counting the number of elements in the list
int sizeOfList(Pgm *p)
{
	int counter = 0;
		
	while(p != NULL)
	{
		counter++;
		//printf("%s\n", *p->pgmlist);
		p = p->next;
	}
	//printf("Nr of commands: %d\n", counter);
	return counter;
}

//A function that just executes the given command
void execute(Command* cmd)
{
	//Execute the command
	if(execvp(*cmd->pgm->pgmlist, cmd->pgm->pgmlist) < 0)
	{
	  printf("Command not understood\n");	
	  exit(1);
	}
}

//Redirect input from a file before executing the command
int input_redirection(Command* cmd)
{
	//Open the file to take input from; return if we fail.
	int in = open(cmd->rstdin, O_RDONLY);
	if(in == -1) {perror("Failed opening requested file"); return 255;}
	
	//Create a duplicate file descriptor of the standard output (always backup before overwriting); return if we fail
	int saveIn = dup(fileno(stdin));
	if (dup2(in, fileno(stdin)) == -1) {perror("Cannot redirect output"); return 255;}
	
	//Execute the command
	execute(cmd);
	
	//Flush the standard output stream and close file;
	fflush(stdout); close(in);
	
	//Get stdin back from backup
	dup2(saveIn, fileno(stdin)); close(saveIn);
	
	//printf("Back to normal input");
	return 0;
}

//Redirect output before executing the command
int output_redirection(Command* cmd)
{
	//Open the file to output to; return if we fail. Use O_APPEND if we want to append instead.
	int out = open(cmd->rstdout, O_RDWR|O_CREAT|O_TRUNC, 0600);
	if(out == -1) {perror("Failed opening requested file"); return 255;}
	
	//Create a duplicate file descriptor of the standard output (always backup before overwriting); return if we fail
	int saveOut = dup(fileno(stdout));
	if (dup2(out, fileno(stdout)) == -1) {perror("Cannot redirect output"); return 255;}
	
	//Execute the command
	execute(cmd);
	
	//Flush the standard output stream and close file;
	fflush(stdout); close(out);
	
	//Get stdout back from backup
	dup2(saveOut, fileno(stdout)); close(saveOut);
	
	//printf("Back to normal output");
	return 0;
}

//Redirect both in- and output before executing the command
int in_out_redirection(Command* cmd)
{
	//Open the file to take input from; return if we fail.
	int in = open(cmd->rstdin, O_RDONLY);
	if(in == -1) {perror("Failed opening requested file"); return 255;}
	//Open the file to output to; return if we fail. Use O_APPEND if we want to append instead.
	int out = open(cmd->rstdout, O_RDWR|O_CREAT|O_TRUNC, 0600);
	if(out == -1) {perror("Failed opening requested file"); return 255;}
	
	//Create a duplicate file descriptor of the standard output; return if we fail
	int saveIn = dup(fileno(stdin));
	if (dup2(in, fileno(stdin)) == -1) {perror("Cannot redirect output"); return 255;}
	
	//Create a duplicate file descriptor of the standard output; return if we fail
	int saveOut = dup(fileno(stdout));
	if (dup2(out, fileno(stdout)) == -1) {perror("Cannot redirect output"); return 255;}
	
	//Execute the command
	execute(cmd);
	
	//Flush the standard output stream and close files;
	fflush(stdout); close(in); close(out);
	
	dup2(saveIn, fileno(stdin)); close(saveIn);
	dup2(saveOut, fileno(stdout)); close(saveOut);
	
	//printf("Back to normal input");
	return 0;	
}

/* IGNORE THIS ONE */
//recursive (not yet working, not used), for future work
int handlePipedRec(char* const* cmds[], size_t pos, int in_fd)
{
	if (cmds[pos + 1] == NULL) 
	{ /* last command */
		dup2(in_fd, STDIN_FILENO); /* read from in_fd, write to STDOUT */
		execvp(cmds[pos][0], cmds[pos]);
		
		return 0;
	}
	else
	{
		int pipefd[2];
		int newPid;		
		//We fork and child executes the first command
		if(pipe(pipefd) == -1)
		{
			perror("Pipe failed\n");
		}
		/* Fork to create a child process that executes our command */
		switch(fork())
		{
		case -1:
			perror("fork failed\n");		
		case 0:					////// Child executes current command
			close(pipefd[0]);
			
			dup2(in_fd, STDIN_FILENO);		//Read from in_fd
			dup2(pipefd[1], STDOUT_FILENO);	//Write to output part of pipe
			execvp(cmds[pos][0], cmds[pos]);
		
		default:				////// Parent executes the rest
			close(pipefd[1]);
			close(in_fd);
			handlePipedRec(cmds, pos + 1, pipefd[0]);
		}
	}
	return 0;
}


//non-recursive
void handlePiped(char ***cmd, Command *commandStruct, int fd_input)
{
    int   pipefd[2];
    pid_t pid;
    int   fd_in = fd_input;
   
    while (*cmd != NULL)
    {
    	if(pipe(pipefd) == -1) {perror("Pipe failed\n");}
        
        if ((pid = fork()) == -1) {perror("Fork failed\n");}
        
        else if (pid == 0)	//CHILD PROCESS
        {
        	//change the input according to the old one
            dup2(fd_in, STDIN_FILENO); 
            
            if (*(cmd + 1) != NULL)
            { 
            	dup2(pipefd[1], STDOUT_FILENO); 
            }
            
            else if((*(cmd + 1) == NULL) && (commandStruct->rstdout != NULL))	// REDIRECT OUTPUT
            {
				pipefd[1] = open(commandStruct->rstdout, O_WRONLY|O_CREAT|O_TRUNC, 0600);
				
				if(pipefd[1] == -1) {perror("Failed opening requested file");}
	            
				if(dup2(pipefd[1], STDOUT_FILENO) == -1) {perror("Cannot redirect output"); return;}
			}
            //Close the unneeded part of the pipe
            close(pipefd[0]);
            
            //Child executes the current command
            if(execvp((*cmd)[0], *cmd) == -1) {perror("Execvp failed");exit(EXIT_SUCCESS);}
            
            
        }
        else	//PARENT PROCESS
        {
            if((commandStruct->bakground != NULL) && (*(cmd + 1) == NULL))
            {
            	//Close the unneeded part of the pipe
            	close(pipefd[1]);
				
            	//Save the input for the next command
				fd_in = pipefd[0]; 
				
				//Move on to next command
				cmd++;
            }
            else
            {
            	wait(NULL);
            	
            	close(pipefd[1]);
            					
				fd_in = pipefd[0]; //save the input for the next command
				
				cmd++;
            }
        }
    }
}

/*
 *  A function that will take the command we sent to our shell, 
 *  turn the list around so it's in the correct order when passed to our pipe and
 *  then check whether we need to handle input redirection or not. If so, we redirect
 *	it to the input of the pipe.
 */
int preparePipe(Command *cmd)
{
	Pgm *head = cmd->pgm;
	    
	char ***inputList = (char***) malloc((sizeOfList(cmd->pgm) + 1) * sizeof(char**));
	
	int len = sizeOfList(cmd->pgm);
	int i = sizeOfList(cmd->pgm);
	
	//turn the list around
	while(i != 0)
	{
		inputList[i-1] = cmd->pgm->pgmlist;
		
		cmd->pgm = cmd->pgm->next;
		
		i--;
	}
	//End the array with a null
	inputList[len] = NULL; 
	
	if(cmd->rstdin != NULL)
	{
		//Open the file to take input from; return if we fail.
		int in = open(cmd->rstdin, O_RDONLY);
		//Create a duplicate file descriptor of the standard output (always backup before overwriting); return if we fail
		int saveIn = dup(fileno(stdin));
		
		if (dup2(in, fileno(stdin)) == -1) {perror("Cannot redirect output"); return 255;}
		
		//Execute the command with input redirection
		handlePiped(inputList, cmd, in);
		
		//Flush the standard output stream and close file;
		fflush(stdout); close(in);
		
		//Get stdin back from backup
		dup2(saveIn, fileno(stdin)); close(saveIn);
		
		//printf("Back to normal input");
	}
	else
	{
		//Execute the command without input redirection
		handlePiped(inputList, cmd, 0);
	}
	
	return 0;
}





/*
 * Name: main
 *
 * Description: Gets the ball rolling...
 *
 */
int main(void)
{
  Command cmd;
  int n;
  
  //Handle ctrl+c
  signal(SIGINT, handle_breaksignal);
  
  //Handle the cleaning up of children
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = &cleanUpChild;
  sigaction(SIGCHLD, &sa, NULL);

  //The point to jump back to
  sigsetjmp(jumpBuffer, 1);
  
  while (!done) 
  {
    char *line;
    char cwd[1024];
    print_WD();
    line = readline("> ");

    if (!line) 
    {
      /* Encountered EOF at top level */
      done = 1;
    }
    else 
    {
      /*
       * Remove leading and trailing whitespace from the line
       * Then, if there is anything left, add it to the history list
       * and execute it.
       */
      stripwhite(line);

      if(*line) 
      {    	  
    	  add_history(line);
      
    	  n = parse(line, &cmd);
    	  //PrintCommand(n, &cmd);
    	  
    	  //Will be checked when we send a SIGINT (ctrl+C) signal 
    	  bgFlag = cmd.bakground;
    	  
    	  //Exit upon request
    	  if(!strcmp(*cmd.pgm->pgmlist, "exit"))
    	  {return 0;}
    	  //PWD upon request
    	  else if(!strcmp(*cmd.pgm->pgmlist, "pwd"))
		  {
    		  print_WD();		  
		  }	  
		  //CD upon request
    	  else if(!strcmp(*cmd.pgm->pgmlist, "cd"))
		  {			      		  
    		  change_WD(cmd.pgm->pgmlist);
		  }    	      	  
    	  //Some other command entered
    	  else
    	  {
    		  if(sizeOfList(cmd.pgm) > 1)
			  {					
    			  //Fork
    			  //Let child get eventual input file and pipe it to the parent
    			  //parent then runs the preparePipe() when child returns
    			  preparePipe(&cmd);
			  }
    		  else
    		  {  
    			  myBash(&cmd);
    		  }
    	  }
      }
    }
    
    if(line) 
    {
      free(line);
    }
  }
  return 0;
}

void myBash(Command *cmd)
{
	/* Fork to create a child process that executes our command */
	if((newPid = fork()) == -1)
	{
	  perror("fork failed for some reason");
	  exit(1);
	}
	
	if(newPid == 0)		//***The CHILD process***//
	{
		//pgid will be the same for the whole program, including the fork
		// unless you intentionally set a new one, using setsid()
		//setsid();
	 
		setpgid(newPid, newPid);
		
	  if(cmd->rstdout != NULL) //DO REDIRECT OUTPUT
	  {
		  if(cmd->rstdin != NULL) //REDIRECT INPUT AS WELL
		  {
			  in_out_redirection(cmd);
		  }
		  else	//ONLY REDIRECT OUTPUT
		  {
			  output_redirection(cmd);  
		  }
	  }
	  else //DON'T REDIRECT OUTPUT
	  {
		  if(cmd->rstdin != NULL) //REDIRECT INPUT
		  {
			  input_redirection(cmd);
		  }				  					 
		  else //DON'T REDIRECT ANYTHING
		  {
			  //Execute the command
			  execute(cmd);			  
		  }
	  }				
	}			//***END OF The CHILD process***//
	
	if(newPid > 0)	//***The PARENT process***//
	{		
		//Run in background and return so we can keep working.
		if(cmd->bakground)
		{		  
			waitpid(newPid, NULL, WNOHANG);
		}
		//Don't run in background, parent must wait
		else
		{
			wait(NULL);
		}
	}			//***END OF The PARENT process***//
}

/*
 * Name: PrintCommand
 *
 * Description: Prints a Command structure as returned by parse on stdout.
 *
 */
void PrintCommand (int n, Command *cmd)
{
  printf("Parse returned %d:\n", n);
  printf("   stdin : %s\n", cmd->rstdin  ? cmd->rstdin  : "<none>" );
  printf("   stdout: %s\n", cmd->rstdout ? cmd->rstdout : "<none>" );
  printf("   bg    : %s\n", cmd->bakground ? "yes" : "no");
  PrintPgm(cmd->pgm);
}

/*
 * Name: PrintPgm
 * Description: Prints a list of Pgm:s
 */
void PrintPgm (Pgm *p)
{
	if (p == NULL) {return;}
	else
	{
		char **pl = p->pgmlist;
		/* The list is in reversed order so print it reversed to get right*/
		PrintPgm(p->next);
		printf("    [");
		while (*pl) {printf("%s ", *pl++);}
		printf("]\n");
	}
}
/*
 * Name: stripwhite
 * Description: Strip whitespace from the start and end of STRING.
 */
void stripwhite (char *string)
{
  register int i = 0;
  while (whitespace( string[i] )) {i++;}
  if (i) {strcpy (string, string + i);}
  i = strlen( string ) - 1;
  while (i> 0 && whitespace (string[i])) {i--;}
  string [++i] = '\0';
}
