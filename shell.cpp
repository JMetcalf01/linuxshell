#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <cstdlib>
#include <string.h>
#include <vector>
#include <sstream>

/*
This is an implementation of a linux shell written by Jonathan Metcalf.
It has many of the basic features of a regular linux shell, but is also missing many.
Several that it doesn't have are:
-Piping
-Variables
-Redirection
-Quoting
-Command completion
*/

using namespace std;

string prompt = "==> ";
 
struct process {
	pid_t pid;
	string command;
	struct timeval start;
};

vector<process> jobs;

vector<string> getInput();
void runCommand(vector<string> args, bool background, struct timeval start, char* envp[]);
void printStats(struct timeval start);

int main(int argc, char* argv[], char* envp[]) {
	// Just so the "[x]" field is not zero-indexed
	process empty;
	jobs.push_back(empty);

	// If there is a command, execute it
	if (argc != 1) {
		// Gets starting time
		struct timeval start;
		gettimeofday(&start, NULL);

		vector<string> args;
		for (int i = 1; i < argc; i++)
			args.push_back(argv[i]);

		runCommand(args, false, start, envp);
	}

	// Now continually prompt the user for input
	while (!cin.eof()) {
		// Checks if any child processes have exited
		// If so, removes them from the jobs list and prints that they are
		pid_t pid;
		while ((pid = waitpid(-1, NULL, WNOHANG)) > 0) {
			for (size_t i = 1; i < jobs.size(); i++) {
				if (jobs[i].pid == pid) {
					printf("[%i] %i Completed\n", i, pid);
					printStats(jobs[i].start);
					jobs[i].pid = -1;
					break;
				}
			}
		}

		vector<string> input = getInput();
		if (input.empty()) continue;
		if (input.size() == 1 && input[0] == "exit") {
			break;
		} else if (input.size() == 4 && input[0] == "set" && input[1] == "prompt" && input[2] == "=") {
			prompt = input[3];
		} else if (input.size() == 2 && input[0] == "cd") {
			if (chdir(input[1].c_str()) < 0)
				cerr << "Chdir error" << endl;
		} else if (input.size() == 1 && input[0] == "jobs") {
			for (size_t i = 1; i < jobs.size(); i++)
				if (jobs[i].pid != -1) 
					printf("[%i] %i %s\n", i, jobs[i].pid, jobs[i].command.c_str());
		} else {
			// Gets starting time
			struct timeval start;
			gettimeofday(&start, NULL);	

			// Checks if it is a background task or not and runs command with "&" removed if so
			bool background = false;
			if (input.back() == "&") {
				background = true;
				input.pop_back();
			}
			runCommand(input, background, start, envp);
		}
	}

	// Wait until all background tasks are done before exiting
	pid_t pid = waitpid(-1, NULL, 0); 
	while (pid != -1) {
		pid = waitpid(-1, NULL, 0); 
	}

	return 0;
}

// Prompts the user for input and returns the string array of command/arguments
vector<string> getInput() {
	string input;
	vector<string> command;

	// Gets input
	cout << prompt;
	getline(cin, input);

	// Splits input by whitespace
	stringstream ss(input);	
	string temp;
	while (ss >> temp)
		command.push_back(temp);

	return command;
}

void runCommand(vector<string> args, bool background, struct timeval start, char* envp[]) {
	int pid;
	if ((pid = fork()) < 0) {
		cerr << "Fork error\n";
		exit(1);
	} else if (pid == 0) {

		// Child process
		const char *c_args[args.size() + 1];
		for (size_t i = 0; i < args.size(); ++i) {
			c_args[i] = args[i].c_str();
		}
		c_args[args.size()] = NULL;

		// Execute command
		// Note the const_cast is because c_str() requires const, 
		// but execvp doesn't like const and is guaranteed not to write to its arguments
		if (execvpe(c_args[0], const_cast<char**>(c_args), envp) < 0) {
			cerr << "Evecve error" << endl;
			exit(1);
		}
	} else {
		// Parent process
		if (background) {
			// Adds new process by replacing unused one (denoted by pid being -1)
			bool replacedUnused = false;
			process newP;
			newP.pid = pid;
			newP.command = args[0];
			newP.start = start;

			for (size_t i = 1; i < jobs.size(); i++) {
				if (jobs[i].pid == -1) {
					replacedUnused = true;
					jobs[i] = newP;
					printf("[%i] %i\n", i, jobs[i].pid);
					break;
				}
			}
			if (!replacedUnused) {
				jobs.push_back(newP);
				printf("[%i] %i\n", jobs.size() - 1, newP.pid);
			}
		} else {
			// Forces shell to sit until command is done
			waitpid(pid, NULL, 0);
		}
	}
	
	if (!background) printStats(start);
}

void printStats(struct timeval start) {
	cout << "\nSTATS:\n";

	// Gets the end time
	struct timeval end;
	gettimeofday(&end, NULL);	

	// Gets resource usage
	struct rusage stats;
	getrusage(RUSAGE_SELF, &stats);

	// Prints user CPU time
	int user_time = (stats.ru_utime.tv_sec * 1000000 + stats.ru_utime.tv_usec) / 1000;
	printf("CPU Time Used (USER): %i ms\n", user_time);

	// Prints system CPU time
	int system_time = (stats.ru_stime.tv_sec * 1000000 + stats.ru_stime.tv_usec) / 1000;
	printf("CPU Time Used (SYSTEM): %i ms\n", system_time);

	// Prints the elapsed "wall-clock" time
	int elapsed_time = ((end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec)) / 1000;
	printf("\"Wall-Clock\" Time Elapsed: %i ms\n", elapsed_time);

	// Prints the number of times the process was preempted involuntarily
	int preempted = stats.ru_nivcsw;
	printf("Preempted Involuntarily: %i times\n", preempted);

	// Prints the number of times the process gave up the CPU voluntarily
	int gave_up = stats.ru_nvcsw;
	printf("Gave Up Voluntarily: %i times\n", gave_up);

	// Prints the number of major page faults
	int major_page_faults = stats.ru_majflt;
	printf("Major Page Faults: %i times\n", major_page_faults);

	// Prints the number of minor page faults
	int minor_page_faults = stats.ru_minflt;
	printf("Minor Page Faults: %i times\n", minor_page_faults);
}
