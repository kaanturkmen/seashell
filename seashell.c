#define _GNU_SOURCE
#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>            //termios, TCSANOW, ECHO, ICANON
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <regex.h>
#include <sys/stat.h>
#include <ctype.h>

const char * sysname = "seashell";

// Flag for understanding if user input is empty or not.
int emptyUserInput = 0;

// GCC Compiling bug "cannot execute ‘cc1’: execvp: No such file or directory"
// has not solved by intentionally since it ruins flags systems of the given code.

// Solution: https://stackoverflow.com/a/41071835 (Tested and it is working.)

enum return_codes {
	SUCCESS = 0,
	EXIT = 1,
	UNKNOWN = 2,
};
struct command_t {
	char *name;
	bool background;
	bool auto_complete;
	int arg_count;
	char **args;
	char *redirects[3]; // in/out redirection
	struct command_t *next; // for piping
};

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t * command)
{
	int i=0;
	printf("Command: <%s>\n", command->name);
	printf("\tIs Background: %s\n", command->background?"yes":"no");
	printf("\tNeeds Auto-complete: %s\n", command->auto_complete?"yes":"no");
	printf("\tRedirects:\n");
	for (i=0;i<3;i++)
		printf("\t\t%d: %s\n", i, command->redirects[i]?command->redirects[i]:"N/A");
	printf("\tArguments (%d):\n", command->arg_count);
	for (i=0;i<command->arg_count;++i)
		printf("\t\tArg %d: %s\n", i, command->args[i]);
	if (command->next)
	{
		printf("\tPiped to:\n");
		print_command(command->next);
	}


}
/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command)
{
	if (command->arg_count)
	{
		for (int i=0; i<command->arg_count; ++i)
			free(command->args[i]);
		free(command->args);
	}
	for (int i=0;i<3;++i)
		if (command->redirects[i])
			free(command->redirects[i]);
	if (command->next)
	{
		free_command(command->next);
		command->next=NULL;
	}
	free(command->name);
	free(command);
	return 0;
}
/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt()
{
	char cwd[1024], hostname[1024];
	gethostname(hostname, sizeof(hostname));
	getcwd(cwd, sizeof(cwd));

	// Created bold and colored shell prompts.
	printf("\033[1m\033[34m%s@%s\033[1m\033[37m:\033[1m\033[32m%s \033[1m\033[36m%s\033[1m\033[37m$ ", getenv("USER"), hostname, cwd, sysname);
	return 0;
}
/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command)
{
	const char *splitters=" \t"; // split at whitespace
	int index, len;
	len=strlen(buf);

	// If user RETURN's before entering any string, setting emptyUserInput to 1.
	if(len == 0) {
		emptyUserInput = 1;
		return SUCCESS;
	}

	while (len>0 && strchr(splitters, buf[0])!=NULL) // trim left whitespace
	{
		buf++;
		len--;
	}
	while (len>0 && strchr(splitters, buf[len-1])!=NULL)
		buf[--len]=0; // trim right whitespace

	if (len>0 && buf[len-1]=='?') // auto-complete
		command->auto_complete=true;
	if (len>0 && buf[len-1]=='&') // background
		command->background=true;

	char *pch = strtok(buf, splitters);
	command->name=(char *)malloc(strlen(pch)+1);
	if (pch==NULL)
		command->name[0]=0;
	else
		strcpy(command->name, pch);

	command->args=(char **)malloc(sizeof(char *));

	int redirect_index;
	int arg_index=0;
	char temp_buf[1024], *arg;
	while (1)
	{
		// tokenize input on splitters
		pch = strtok(NULL, splitters);
		if (!pch) break;
		arg=temp_buf;
		strcpy(arg, pch);
		len=strlen(arg);

		if (len==0) continue; // empty arg, go for next
		while (len>0 && strchr(splitters, arg[0])!=NULL) // trim left whitespace
		{
			arg++;
			len--;
		}
		while (len>0 && strchr(splitters, arg[len-1])!=NULL) arg[--len]=0; // trim right whitespace
		if (len==0) continue; // empty arg, go for next

		// piping to another command
		if (strcmp(arg, "|")==0)
		{
			struct command_t *c=malloc(sizeof(struct command_t));
			int l=strlen(pch);
			pch[l]=splitters[0]; // restore strtok termination
			index=1;
			while (pch[index]==' ' || pch[index]=='\t') index++; // skip whitespaces

			parse_command(pch+index, c);
			pch[l]=0; // put back strtok termination
			command->next=c;
			continue;
		}

		// background process
		if (strcmp(arg, "&")==0)
			continue; // handled before

		// handle input redirection
		redirect_index=-1;
		if (arg[0]=='<')
			redirect_index=0;
		if (arg[0]=='>')
		{
			if (len>1 && arg[1]=='>')
			{
				redirect_index=2;
				arg++;
				len--;
			}
			else redirect_index=1;
		}
		if (redirect_index != -1)
		{
			command->redirects[redirect_index]=malloc(len);
			strcpy(command->redirects[redirect_index], arg+1);
			continue;
		}

		// normal arguments
		if (len>2 && ((arg[0]=='"' && arg[len-1]=='"')
					|| (arg[0]=='\'' && arg[len-1]=='\''))) // quote wrapped arg
		{
			arg[--len]=0;
			arg++;
		}
		command->args=(char **)realloc(command->args, sizeof(char *)*(arg_index+1));
		command->args[arg_index]=(char *)malloc(len+1);
		strcpy(command->args[arg_index++], arg);
	}
	command->arg_count=arg_index;
	return 0;
}
void prompt_backspace()
{
	putchar(8); // go back 1
	putchar(' '); // write empty over
	putchar(8); // go back 1 again
}
/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command)
{
	int index=0;
	char c;
	char buf[4096];
	static char oldbuf[4096];

	// tcgetattr gets the parameters of the current terminal
	// STDIN_FILENO will tell tcgetattr that it should write the settings
	// of stdin to oldt
	static struct termios backup_termios, new_termios;
	tcgetattr(STDIN_FILENO, &backup_termios);
	new_termios = backup_termios;
	// ICANON normally takes care that one line at a time will be processed
	// that means it will return if it sees a "\n" or an EOF or an EOL
	new_termios.c_lflag &= ~(ICANON | ECHO); // Also disable automatic echo. We manually echo each char.
	// Those new settings will be set to STDIN
	// TCSANOW tells tcsetattr to change attributes immediately.
	tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);


	//FIXME: backspace is applied before printing chars
	show_prompt();
	int multicode_state=0;
	buf[0]=0;
	while (1)
	{
		c=getchar();
		// printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

		if (c==9) // handle tab
		{
			buf[index++]='?'; // autocomplete
			break;
		}

		if (c==127) // handle backspace
		{
			if (index>0)
			{
				prompt_backspace();
				index--;
			}
			continue;
		}
		if (c==27 && multicode_state==0) // handle multi-code keys
		{
			multicode_state=1;
			continue;
		}
		if (c==91 && multicode_state==1)
		{
			multicode_state=2;
			continue;
		}
		if (c==65 && multicode_state==2) // up arrow
		{
			int i;
			while (index>0)
			{
				prompt_backspace();
				index--;
			}
			for (i=0;oldbuf[i];++i)
			{
				putchar(oldbuf[i]);
				buf[i]=oldbuf[i];
			}
			index=i;
			continue;
		}
		else
			multicode_state=0;

		putchar(c); // echo the character
		buf[index++]=c;
		if (index>=sizeof(buf)-1) break;
		if (c=='\n') // enter key
			break;
		if (c==4) // Ctrl+D
			return EXIT;
	}

	if (index>0 && buf[index-1]=='\n') // trim newline from the end
		index--;
	buf[index++]=0; // null terminate string

	strcpy(oldbuf, buf);

	parse_command(buf, command);

	//print_command(command); // DEBUG: uncomment for debugging

	// restore the old settings
	tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
	return SUCCESS;
}
int process_command(struct command_t *command);
int main()
{
	while (1)
	{
		struct command_t *command=malloc(sizeof(struct command_t));
		memset(command, 0, sizeof(struct command_t)); // set all bytes to 0

		int code;
		code = prompt(command);
		if (code==EXIT) break;

		code = process_command(command);
		if (code==EXIT) break;

		free_command(command);
	}

	printf("\n");
	return 0;
}

int validateGoodMorningArgs(char *time, char *path) {
	// Creating variable for regex and stat structure for future use.
	regex_t regex_time;
	struct stat file;

	// Defining new regex as XX.XX where X represents a digit.
	regcomp(&regex_time, "^[0-9][0-9][.][0-9][0-9]$", 0);

	// Checking if use parameters are valid and returning EXIT if they are not.
	if ((stat(path, &file) < 0) || regexec(&regex_time, time, 0, NULL, 0)) return EXIT;

	// Returning SUCCESS if they are valid.
	return SUCCESS;
}

void executeGoodMorning(char *time, char *path) {
	if(!validateGoodMorningArgs(time, path)) {

		// Char array for extracting minute and hour.
		char min[3];
		char hour[3];

		// Extracting required hour and min.
		strncpy(hour, time, 2);
		min[0] = time[3];
		min[1] = time[4];
		min[2] = '\0';

		// Creating a file pointer.
		FILE *fp;

		// Creating / Writing file for inputting crontab.
		fp = fopen("crontab.txt", "w");

		// https://askubuntu.com/a/483920 //
		// export XAUTHORITY && export DISPLAY=:0 part of the code is taken from above link. //
		// Writing a crontab task which calls rhythmbox-client and plays music.
		fprintf(fp, "%d %d * * * export XAUTHORITY && export DISPLAY=:0 && rhythmbox-client %s --play\n", atoi(min), atoi(hour), path);
		fprintf(fp, "%d %d * * * export XAUTHORITY && export DISPLAY=:0 && rhythmbox-client --exit\n", atoi(min) + 1, atoi(hour));
		fprintf(fp, "%d %d * * * crontab -r\n", atoi(min) + 1, atoi(hour));

		// Closing file pointer.
		fclose(fp);

		int stat;
		// Creating status variable and forking a new child.
		int childPID = fork();

		if (childPID < 0) {
			printf("goodMorning: Failed to fork.\n");
			return;
		}

		// Making child to exec the cronjob.
		if (childPID == 0) {
			execl("/bin/crontab", "crontab", "crontab.txt", NULL);

			// Removing crontab.txt since it is not required after running crontab.
			remove("crontab.txt");
		} else {
			waitpid(childPID, &stat, 0);
		}

		// Printing operation is successful message.
		printf("SUCCESS: Your alarm has been set.\n");

	} else {
		// Printing if user fails to enter valid inputs. Showing a valid inputs.
		printf("-%s: goodMorning: Please use valid inputs.\n", sysname);
		printf("-%s: goodMorning: Example Usage: goodMorning 07.21 /home/kaan/Desktop/hello_COMP304.mp3\n", sysname);
	}
}

int validateKDiffArgs(char **args, int argCount) {
	// Creating file structure and char pointer for further use.
	struct stat file;
	char *extensionPointer;

	// If argCount is equal to 2, we are using default mode which is non-binary comparison.
	if (argCount == 2) {

		// Checking if paths are valid.
		if (stat(args[0], &file) < 0) return EXIT;
		if (stat(args[1], &file) < 0) return EXIT;

		// Checking for files which do not have extension.
		if(strchr(args[0], '.') == NULL) return EXIT;
		if(strchr(args[0], '.') == NULL) return EXIT;

		// Checking if their extension is txt.
		extensionPointer = strchr(args[0], '.');
		extensionPointer++;
		printf("%s\n", extensionPointer);
		if (!strstr(extensionPointer, "txt")) return EXIT;

		extensionPointer = strchr(args[1], '.');
		extensionPointer++;
		if (!strstr(extensionPointer, "txt")) return EXIT;

		// If argumentCount is 3, then there is a flag.
	} else if (argCount == 3) {

		// Checking if paths are valid.
		if (stat(args[1], &file) < 0) return EXIT;
		if (stat(args[2], &file) < 0) return EXIT;

		// If user gave another flag except -a and -b, terminating the command.
		if (strcmp(args[0], "-a") && strcmp(args[0], "-b")) return EXIT;

		// If mode a is selected, checking if file has .txt extension.
		if (!strcmp(args[0], "-a")) {
			extensionPointer = strchr(args[1], '.');
			extensionPointer++;
			if (!strstr(extensionPointer, "txt")) return EXIT;

			extensionPointer = strchr(args[2], '.');
			extensionPointer++;
			if (!strstr(extensionPointer, "txt")) return EXIT;
		}
	}

	return SUCCESS;
}

void executeKDiff(char **args, int argCount) {

	// Executing kdiff method if arguments are valid.
	if(!validateKDiffArgs(args, argCount)) {

		// Creating file pointers for further use.
		FILE *fp1;
		FILE *fp2;

		// Checking if mode is binary.
		int binaryFlag = 0;

		// Opening files according to given flags.
		if (argCount == 3 && !strcmp(args[0], "-b")) {
			fp1 = fopen(args[1], "rb");
			fp2 = fopen(args[2], "rb");
			binaryFlag = 1;
		} else if (argCount == 3 && !strcmp(args[0], "-a")) {
			fp1 = fopen(args[1], "r");
			fp2 = fopen(args[2], "r");
		} else {
			fp1 = fopen(args[0], "r");
			fp2 = fopen(args[1], "r");
		}

		// Creating a temp content to store lines.
		char tempContent1[100];
		char tempContent2[100];

		// Declaring char to store each byte.
		char firstByte;
		char secondByte;

		// Creating count and lineCount variables.
		int count = 0;
		int lineCount = 0;

		// Iterating until file ends.
		while(!feof(fp1) && !feof(fp2)) {
			if (!binaryFlag) {
				// Getting lines and comparing them with each other.
				if((fgets(tempContent1, 100, fp1) != NULL) && (fgets(tempContent2, 100, fp2) != NULL)) {
					if(strcmp(tempContent1, tempContent2)) {
						printf("\nDifference spotted: Line %d: File1.txt %s", (lineCount + 1), tempContent1);
						printf("Difference spotted: Line %d: File2.txt %s\n", (lineCount + 1), tempContent2);
						count++;
					}
				}
				lineCount++;
			} else {

				// Getting byte from each file.
				firstByte = fgetc(fp1);
				secondByte = fgetc(fp2);

				// Checking if they are equal.
				if (firstByte != secondByte) count++;
			}
		}

		// Printing information about files.
		if (!binaryFlag) {
			if(count != 0) {
				printf("Total different line count is %d\n", count);
			} else {
				printf("Given files are identical.\n");
			}
		} else {
			if(count != 0) {
				printf("Total byte difference between two file is %d \n", count);
			} else {
				printf("Given files are identical.\n");
			}
		}

		// Closing file pointers.
		fclose(fp1);
		fclose(fp2);
	} else {
		// Error message is being prompted if user inputted invalid arguments.
		printf("-%s: kdiff: Please use valid paths or flags. (Use .txt extension only for the non-binary mode.)\n", sysname);
	}
}

int validateHighlight(char **args, int argCount) {
	
	if(argCount != 3) {
		printf("highlight: Argument count should be exactly equal to the 3.\n");
		return EXIT;
	}

	if(strcmp(args[1], "r") && strcmp(args[1], "g") && strcmp(args[1], "b")) {
		printf("highlight: Second argument should be r, g or b.\n");
		return EXIT;
	}

	struct stat file;

	if(stat(args[2], &file) < 0) {
		printf("highlight: Please input a legit path.\n");
		return EXIT;
	}

	return SUCCESS;
}

void executeHighlight(char **args, int argCount) {
	if(!validateHighlight(args, argCount)) {

		// Color codes.
		char boldRed[20] = "\033[1m\033[31m";
		char boldGreen[20] = "\033[1m\033[32m";
		char boldBlue[20] = "\033[1m\033[34m";
		char white[20] = "\033[37m";

		// Extracted words is created to add space to both sides of the word
		// thus, it is only finding word occurances and not a part of the word.
		char extractedWord[100] = " ";

		strcat(extractedWord, args[0]);

		strcat(extractedWord, " ");

		// Creating arrays for further use.
		char sentences[100];
		char rightSide[100];
		char leftSide[100];
		char finalOutput[100];
		char originalWord[100];

		// Creating file pointer.
		FILE *fp1;

		// Creating char pointer to do string operations.
		char *wordLocation;

		// Opening the file.
		fp1 = fopen(args[2], "r");

		// Reading file until it reachs EOF, getting the line and searches for
		// the given word, if it is found, creates required string splitting
		// adding color codes and combining them again.
		while(!feof(fp1)) {
			if(fgets(sentences, 100, fp1) != NULL) {
				wordLocation = strcasestr(sentences, extractedWord);
				strncpy(originalWord, wordLocation, strlen(args[0]) + 1);

				if (wordLocation != NULL) {
					*wordLocation = '\0';
					strcpy(leftSide, sentences);

					strcpy(rightSide, (wordLocation + strlen(args[0]) + 1));

					strcpy(finalOutput, leftSide);

					if(!strcmp(args[1], "r")) strcat(finalOutput, boldRed);
					else if(!strcmp(args[1], "g")) strcat(finalOutput, boldGreen);
					else if(!strcmp(args[1], "b")) strcat(finalOutput, boldBlue);

					strcat(finalOutput, originalWord);

					strcat(finalOutput, white);

					strcat(finalOutput, rightSide);

					printf("%s\n", finalOutput);

					wordLocation = NULL;
				}
			}
		}
	}
}


void executeCStock(char **args, int argCount) {

	// Creating string for the URL.
	char tempURL[100];

	// Copying default URL to the string.
	strcpy(tempURL, "rate.sx/");

	// Creating childPID and stat variables for further use.
	pid_t childPID;
	int stat;

	// Created a child process.
	childPID = fork();

	// Prompting an error if creating process operation fails.
	if(childPID < 0) {
		printf("cstock: Failed to fork.\n");
		return;
	}

	// Child process is doing the executing job
	if(childPID == 0) {

		if (argCount == 1 && !strcmp(args[0], "--help")) {
			printf("\ncstock: Graph Mode -> cstock [Crypto Currency Name] [Day (Optional) Range: [1-90]]\n");
			printf("cstock: Example: cstock eth 4\n");
			printf("cstock: to view last 4 day activity of ethereum.\n\n");

			printf("\ncstock: Table Mode -> stock -a\n");
			printf("cstock: to view available currencies' table.\n\n");
		} else if (argCount == 1 && !strcmp(args[0], "-a")) {
			execl("/usr/bin/curl", "-d", tempURL, NULL);
		} else if (argCount == 1) {
			strcat(tempURL, args[0]);
			execl("/usr/bin/curl", "-d", tempURL, NULL);
		} else if (argCount == 2) {

			// Iterating parameter char by char and checking if its aa digit.
			int argLen = strlen(args[1]);
			int strIsDigit = 1;

			for (int i = 0; i<argLen; i++) {
				if(!isdigit(args[1][i])) {
					strIsDigit = 0;
					break;
				}
			}

			if(strIsDigit) {
				if(atoi(args[1]) > 90 || atoi(args[1]) <= 0) {
					printf("cstock: Your day parameter should be in rage [1-90]. Please try again.\n");
					return;
				}
				strcat(tempURL, args[0]);
				strcat(tempURL, "@");
				strcat(tempURL, args[1]);
				strcat(tempURL, "d");
				execl("/usr/bin/curl", "-d", tempURL, NULL);
			} else {
				printf("cstock: Please use positive number input to the second parameter. Range: [1-90]\n");
			}
		} else {
			printf("\ncstock: Missing, too many, or invalid parameter.\n");
			printf("cstock: Usage: cstock <param1> ...\n\n");
			printf("cstock: Try 'cstock --help' for more options.\n\n");
		}
	} else {
		waitpid(childPID, &stat, 0);
	}
}

int process_command(struct command_t *command)
{
	int r;

	if (!emptyUserInput) {

		if (strcmp(command->name, "")==0) return SUCCESS;

		if (strcmp(command->name, "exit")==0)
			return EXIT;

		if(!strcmp(command->name, "highlight")) {
			executeHighlight(command->args, command->arg_count);
			return SUCCESS;
		}

		if(!strcmp(command->name, "cstock")) {
			executeCStock(command->args, command->arg_count);
			return SUCCESS;
		}

		if (strcmp(command->name, "goodMorning") == 0) {
			if (command->arg_count != 2) {
				printf("-%s: %s: Please use exactly 2 parameters as an input.\n", sysname, command->name);
			} else {
				executeGoodMorning(command->args[0], command->args[1]);
			}
			return SUCCESS;
		}

		if (strcmp(command->name, "kdiff") == 0) {
			if ((command->arg_count <= 1) || command->arg_count > 3 ) {
				printf("-%s: %s: Please use minimum 2 and maximum 3 parameters as an input.\n", sysname, command->name);
			} else {
				executeKDiff(command->args, command->arg_count);
			}
			return SUCCESS;
		}

		if (strcmp(command->name, "cd")==0)
		{
			if (command->arg_count > 0)
			{
				r=chdir(command->args[0]);
				if (r==-1)
					printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
				return SUCCESS;
			}
		}

		pid_t pid=fork();
		if (pid==0) // child
		{
			/// This shows how to do exec with environ (but is not available on MacOs)
			// extern char** environ; // environment variables
			// execvpe(command->name, command->args, environ); // exec+args+path+environ

			/// This shows how to do exec with auto-path resolve
			// add a NULL argument to the end of args, and the name to the beginning
			// as required by exec

			// increase args size by 2
			command->args=(char **)realloc(
					command->args, sizeof(char *)*(command->arg_count+=2));

			// shift everything forward by 1
			for (int i=command->arg_count-2;i>0;--i)
				command->args[i]=command->args[i-1];

			// set args[0] as a copy of name
			command->args[0]=strdup(command->name);
			// set args[arg_count-1] (last) to NULL
			command->args[command->arg_count-1]=NULL;

			// execvp(command->name, command->args); // exec+args+path
			// exit(0);
			/// TODO: do your own exec with path resolving using execv()

			// Getting all environments.
			char *environments = getenv("PATH");

			// Creating an 2D environment array.
			char environmentArray[30][30];

			// Creating new variable for tokenized strings.
			char *tokenizedString;

			// Getting current directory.
			char *currentDirectory = getenv("PWD");

			// Environment count for environment array indexing.
			int envCount = 0;

			// First searching if program is executable. And testing if it exists
			// in current directory.
			strcat(currentDirectory, "/");
			strcat(currentDirectory, command->name);

			// Filling 2D array.
			tokenizedString = strtok(environments, ":");

			while (tokenizedString != NULL ) {
				strcpy(environmentArray[envCount++], tokenizedString);
				tokenizedString = strtok(NULL, ":");
			}

			// Checking if its executable or if it exists in current directory.
			if (execv(currentDirectory, command->args) != -1) exit(0);

			// Else searching inside of path directories.
			for(int i = 0; i<envCount; i++) {
				strcat(environmentArray[i], "/");
				strcat(environmentArray[i], command->name);
				if (execv(environmentArray[i], command->args) != -1) exit(0);
			}

			// If command is not found in any of system paths or current path, printing error message.
			printf("-%s: %s: command not found\n", sysname, command->name);
			exit(0);

		}
		else
		{
			if (!command->background)
				wait(0); // wait for child process to finish
			return SUCCESS;
		}

		// TODO: your implementation here

		printf("-%s: %s: command not found\n", sysname, command->name);
		return UNKNOWN;
	} else {
		// Setting emptyUserInput is 0 since it is not empty anymore.
		emptyUserInput = 0;
		return SUCCESS;
	}
}
