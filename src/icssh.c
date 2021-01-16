#include "icssh.h"
#include "linkedList.h"
#include <readline/readline.h>

List_t* bglist;

int readyToReap = 0;

void sigchld_handler(int signal) {
	readyToReap = 1;
}

void sigusr2_handler(int signal) {
	pid_t pid = getpid();
	printf("Hi User! I am process %d\n", pid);
}

int checkFileExist(char *fileName) {
	FILE * file;
	if (file = fopen(fileName, "r")){
        fclose(file);
		return 1;
    }
	return 0;
}

void checkRedirection(proc_info* proc){
	if (proc->in_file)
	{
		if (proc->out_file)
		{
			if (strcmp(proc->in_file, proc->out_file) == 0)
			{
				fprintf(stderr, RD_ERR);
				validate_input(NULL);
			}
		}
		if (proc->err_file)
		{
			if (strcmp(proc->in_file, proc->err_file) == 0)
			{
				fprintf(stderr, RD_ERR);
				validate_input(NULL);
			}
		}
		if (proc->outerr_file)
		{
			if (strcmp(proc->in_file, proc->outerr_file) == 0)
			{
				fprintf(stderr, RD_ERR);
				validate_input(NULL);
			}
		}
	}
	if (proc->out_file)
	{
		if (proc->err_file)
		{
			if (strcmp(proc->out_file, proc->err_file) == 0)
			{
				fprintf(stderr, RD_ERR);
				validate_input(NULL);
			}
		}
		if (proc->outerr_file)
		{
			if (strcmp(proc->out_file, proc->outerr_file) == 0)
			{
				fprintf(stderr, RD_ERR);
				validate_input(NULL);
			}
		}
	}
	if (proc->err_file)
	{
		if (proc->outerr_file)
		{
			if (strcmp(proc->err_file, proc->outerr_file) == 0)
			{
				fprintf(stderr, RD_ERR);
				validate_input(NULL);
			}
		}
	}
}

int main(int argc, char* argv[]) {
	int exec_result;
	int exit_status;
	int fd[2];
	pid_t pid;
	pid_t wait_result;
	char* line;
	bglist = malloc(sizeof(List_t));
	bglist->length = 0;
    bglist->head = NULL;
	int inputOn = 0;
	char inputFile[80];

#ifdef GS
    rl_outstream = fopen("/dev/null", "w");
#endif

	// Setup segmentation fault handler
	if (signal(SIGSEGV, sigsegv_handler) == SIG_ERR) {
		perror("Failed to set signal handler");
		exit(EXIT_FAILURE);
	}

	if (signal(SIGCHLD, sigchld_handler) == SIG_ERR){
		perror("Failed to set signal handler");
		exit(EXIT_FAILURE);
	}

	if (signal(SIGUSR2, sigusr2_handler) == SIG_ERR){
		perror("Failed to set signal handler");
		exit(EXIT_FAILURE);
	}

    // print the prompt & wait for the user to enter commands string
	while ((line = readline(SHELL_PROMPT)) != NULL) {
        // MAGIC HAPPENS! Command string is parsed into a job struct
        // Will print out error message if command string is invalid
		job_info* job = validate_input(line);
        if (job == NULL) { // Command was empty string or invalid
			free(line);
			continue;
		}
        //Prints out the job linked list struture for debugging
        #ifdef DEBUG   // If DEBUG flag removed in makefile, this will not longer print
            debug_print_job(job);
        #endif

		if (readyToReap == 1)
		{
			node_t* current = bglist->head;
			int i = 0;
			while (current != NULL)
			{
				bgentry_t * currentBg = (bgentry_t *)current->value;
				if (currentBg->pid == pid)
				{
					printf(BG_TERM, pid, currentBg->job->line);
					free_job(currentBg->job);
					free(currentBg);
					removeByIndex(bglist, i);
					wait_result = waitpid(pid, &exit_status, WNOHANG);
					if (wait_result < 0) {
						printf(WAIT_ERR);
						exit(EXIT_FAILURE);
					}
					i--;
				}
				current = current->next;
				i++;
			}
			readyToReap = 0;
		}



		// example built-in: exit
		if (strcmp(job->procs->cmd, "exit") == 0) {
			// Terminating the shell
			free(line);
			free_job(job);
            validate_input(NULL);
			if (bglist->head != NULL)
			{
				while (bglist->head != NULL)	// kill everything
				{
					bgentry_t * currentBg = (bgentry_t *)bglist->head->value;
					printf(BG_TERM, currentBg->pid, currentBg->job->line);
					kill(currentBg->pid, SIGKILL);
					free_job(currentBg->job);
					free(bglist->head->value);
					removeFront(bglist);
				}
			}
            return 0;
		}

		if (strcmp(job->procs->cmd, "cd") == 0){ // Doesnt support cd and cd .
			if (job->procs->argc > 1)
			{
				int i = chdir(job->procs->argv[1]);
				if (i == 0)
				{
					char s[100]; 
					printf("%s\n", getcwd(s, 100)); 
				}
				else
				{
					fprintf(stderr, DIR_ERR);
				}
			}
			else{
				int i = chdir(getenv("HOME"));
				if (i == 0)
				{
					char s[100]; 
					printf("%s\n", getcwd(s, 100)); 
				}
				else
				{
					fprintf(stderr, DIR_ERR);
				}
			}
			free_job(job);	
			continue;
			
		}

		if (strcmp(job->procs->cmd, "bglist") == 0){
			if (bglist->length > 0) {
				node_t* current = bglist->head;
				while (current != NULL)
				{
					print_bgentry((bgentry_t *)current->value);
					current = current->next;
				}
			}
			continue;
		}

		if (strcmp(job->procs->cmd, "estatus") == 0){
			int recent_exit_status = WEXITSTATUS(exit_status);
			printf("%d\n", recent_exit_status);
			free_job(job);
			continue;
		}

		// example of good error handling!
		if ((pid = fork()) < 0) {
			perror("fork error");
			exit(EXIT_FAILURE);
		}
		if (pid == 0) {  //If zero, then it's the child process
            //get the first command in the job list
		    proc_info* proc = job->procs;

			checkRedirection(proc);

			if (proc->in_file)
			{
				if (checkFileExist(proc->in_file))
				{
					int in;
					in = open(proc->in_file, O_RDONLY);
					dup2(in, STDIN_FILENO);
					close(in);
				}
				else
				{
					fprintf(stderr, RD_ERR);
					validate_input(NULL);
					free_job(job);  
					free(line);
					exit(EXIT_FAILURE);
				}	
			}
			if (proc->out_file)
			{
				int out;
				out = open(proc->out_file, O_WRONLY | O_CREAT |O_TRUNC);
				dup2(out, STDOUT_FILENO);
				close(out);
			}
			if (proc->err_file)
			{
				int out;
				out = open(proc->err_file, O_WRONLY | O_CREAT | O_TRUNC);
				dup2(out, STDERR_FILENO);
				close(out);
			}
			if (proc->outerr_file)
			{
				int out;
				out = open(proc->outerr_file, O_WRONLY | O_CREAT | O_TRUNC);
				dup2(out, STDERR_FILENO);
				dup2(out, STDOUT_FILENO);
				close(out);
			}
			if (job->nproc == 1)
			{
				exec_result = execvp(proc->cmd, proc->argv);
			}
			else if (job->nproc > 1)
			{
				int fd[2];	// Set Pipe
				pipe(fd);
				if ((pid = fork()) < 0) {
					perror("fork error");
					exit(EXIT_FAILURE);
				}
				if (pid == 0)
				{
					if (job->nproc == 3){
						int fd2[2];
						pipe(fd2);
						if ((pid = fork()) < 0) {
							perror("fork error");
							exit(EXIT_FAILURE);
						}

						if (pid == 0) // The Great Grandchild
						{
							dup2(fd2[1], STDOUT_FILENO);
							close(fd2[0]);
							exec_result = execvp(proc->cmd, proc->argv);
						} 
						else { // The Grandchild
							dup2(fd2[0], STDIN_FILENO);
							dup2(fd[1], STDOUT_FILENO);
							close(fd2[1]);
							close(fd[0]);
							exec_result = execvp(proc->next_proc->cmd, proc->next_proc->argv);
						}
					}

					else { // Pipe has only 2 ends
						dup2(fd[1], STDOUT_FILENO);
						close(fd[0]);
						exec_result = execvp(proc->cmd, proc->argv);

					}
				} else { // The Child

					if (job->nproc == 3){	// If there are three processes
						dup2(fd[0], STDIN_FILENO);
						close(fd[1]);
						exec_result = execvp(proc->next_proc->next_proc->cmd, proc->next_proc->next_proc->argv);

					}
					else{					// If there are two processes
						dup2(fd[0], STDIN_FILENO);
						close(fd[1]);
						exec_result = execvp(proc->next_proc->cmd, proc->next_proc->argv);
					}
				}
			}
			if (exec_result < 0) {  //Error checking
				printf(EXEC_ERR, proc->cmd);
				// Cleaning up to make Valgrind happy 
				// (not necessary because child will exit. Resources will be reaped by parent or init)
				free_job(job);  
				free(line);
    			validate_input(NULL);
				exit(EXIT_FAILURE);
			}



		} else {
            // As the parent, wait for the foreground job to finish
			if (job->bg == true)
			{
				
				bgentry_t* newBg = malloc(sizeof(bgentry_t));
				newBg->job = job;
				newBg->pid = pid;
				newBg->seconds = time(NULL);
				void *pointer = newBg;
				insertRear(bglist, pointer);
				free(line);
			}
			else
			{
				wait_result = waitpid(pid, &exit_status, 0);
				if (wait_result < 0) {
					printf(WAIT_ERR);
					exit(EXIT_FAILURE);
				}
			}
		}




		if (job->bg == false)
		{
			free_job(job);  // if a foreground job, we no longer need the data
			free(line);
		}
	}

    // calling validate_input with NULL will free the memory it has allocated
    validate_input(NULL);

#ifndef GS
	fclose(rl_outstream);
#endif
	return 0;
}
