
#include "format.h"
#include "shell.h"
#include "vector.h" 
#include "sstring.h"
#include "signal.h"
#include "string.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define D 0
#define E stderr
typedef struct process {
    char *command;
    int pid;
    time_t start;
    struct tm* time;
} process;
static vector *process_vec;

// CHECK BEFORE COMMIT FOR OTHER FILE CHANGES BY ACCIDENT
// GO BACK THROUGH AND MAKE SURE PATHS ARE ABS
// TODO: Consider creating a new struct that handles globals for the shell
// TODO: ADD process managment with the process_info struct ASK: What's the recommended useage.
// TODO: Set up signals
// TODO: Set up write on exit
typedef struct history {
    char* history_path;
    vector *history_vector;
    int history_handle;
} history;


// Functions and globals for history
static history *hist;
static int flag_history = 0;
static char* path_history = NULL;
history* history_create(const char* file_path);
void history_write(history *this); // We are going to write on shell exits
void history_print(history *this);
int history_index(history *this, int index);
int history_prefix(history *this, const char* prefix);
void history_push(char* command);

// Functions and globals for directory operations
static char cwd[PATH_MAX];
int change_directory(const char* new_dir);

// Functions and globals for processes and exec
int execute_command(char* command);

// Functions and globals for scripts
static int flag_script = 0;
static char* path_script =  NULL;
void execute_script(const char* file_path); // TODO: Add error handled and execute_command

// Functions and globals for SIGNALS
static volatile sig_atomic_t program_interrupt = 0;
static bool program_exit = 0;
// Function used for catching SIGINT
void handle_sigint(int signal) {
    program_interrupt = 1;
    process* temp_process = *vector_back(process_vec);
    process* base = vector_get(process_vec, 0);
    if(temp_process->pid == base->pid){
        if(D)fprintf(E,"DEBUG Tried to kill shell with SIGINT! %d vs %d\n", temp_process->pid, base->pid);
    }else{
        print_killed_process(temp_process->pid, temp_process->command);
        kill(temp_process->pid, SIGKILL);
        vector_pop_back(process_vec);
    }
    program_interrupt = 0;

}

void handle_program_exit(); // TODO: Write this logic!



void kill_proc(int pid) {
    int p_size = vector_size(process_vec);
    bool found = 0;
    int i = 0;
    process* p_temp;
    for(; i < p_size; i++){
        p_temp = vector_get(process_vec, i);
        if(p_temp->pid == pid){
            found = 1;
            break;
        }
    }
    if(found == 0){
        print_no_process_found(pid);
    } else{
        int out = kill(pid, SIGKILL);
        print_killed_process(pid, p_temp->command);
        if(out == 0){
            vector_erase(process_vec, i);
        }
    }
}

void stop_proc(int pid) {
    int p_size = vector_size(process_vec);
    bool found = 0;
    int i = 0;
    process* p_temp;
    for(; i < p_size; i++){
        p_temp = vector_get(process_vec, i);
        if(p_temp->pid == pid){
            found = 1;
            break;
        }
    }
    if(found == 0){
        print_no_process_found(pid);
    } else{
        kill(pid, SIGSTOP);
        print_stopped_process(pid, p_temp->command);
    }
}

void cont_proc(int pid) {
    if(kill(pid, SIGCONT) != 0){
        print_no_process_found(pid);
    }
}



// Function used to set up vars used for process, working directory
void init() {
    program_interrupt = 0;
    program_exit = 0;
    getcwd(cwd, sizeof(cwd));
    process_vec = malloc(sizeof(process*) * 256); // ASK: Do I need to malloc?, what size?
    process_vec = shallow_vector_create();

    process *base = (process*) malloc(sizeof(process));
    base->command = strdup("./shell");
    base->pid = (int) getpid();
    // /proc/pid/status

    time_t now;
    time(&now);
    base->start = now;
    base->time = malloc(sizeof(struct tm));
    base->time = localtime(&now);
    vector_push_back(process_vec, base);

}

// Function used to detect the starting shell flags
void parse_args(int argc, char *argv[])  {
    extern char* optarg;
    int opt_out;
    opterr = 0;
    while( (opt_out = getopt(argc, argv, "h:f:")) != -1 ){ // ASK: Do we exit if usage is invalid?
        switch (opt_out) {
            case 'h':
                if( !strcmp(optarg, "") || optarg == NULL) {
                    print_usage(); // ASK: Do we need to handle program exit here?
                    exit(1);
                }
                flag_history = 1;
                path_history = strdup(get_full_path(optarg));
                if(D)fprintf(E,"DEBUG HISTORY FILE IS: |%s|\n", path_history);
                break;

            case 'f':
                if( !strcmp(optarg, "") || optarg == NULL) {
                    print_usage();
                    exit(1);
                }
                flag_script = 1;
                path_script = strdup(get_full_path(optarg));
                break;
        }
    }
}

void print_processes(){
    print_process_info_header();
    for(size_t i = 0; i < vector_size(process_vec); i++){
        process* curr = vector_get(process_vec, i);
        //if(D)fprintf(E, "DEBUG: Curr: %d...\n", curr->pid);
        char *proc_path;

        char *buffer = NULL;
        size_t length = 0;
        ssize_t nchars;

        asprintf(&proc_path,"/proc/%d/stat",curr->pid);
        FILE* temp_proc = fopen(proc_path, "r");
        if(temp_proc == NULL){
            //if(D)fprintf(E, "DEBUG: TEMP PROC FAILURE %d...\n", curr->pid);
            continue;
        }

        nchars = getline(&buffer, &length, temp_proc);
        sstring* sstr = cstr_to_sstring(buffer);
        vector* stats  = sstring_split(sstr, ' ');
        struct process_info* info = malloc(sizeof(process_info));
        info->pid = curr->pid;
        // We need to convert these to the correct types
        info->nthreads = atol( vector_get(stats, 19) );
        info->vsize = (unsigned long int) atol( vector_get(stats, 22));
        memcpy(&(info->state),vector_get(stats, 2),  1);

    
        time_t timey;
        time(&timey);
        //struct tm* start_time = atol(vector_get(stats, 21));
        info->start_str = calloc(8, 1);
        
        time_struct_to_string(info->start_str, 8, curr->time);

        info->time_str = calloc(8, 1);

        double difference = difftime(timey, curr->start);

        size_t minutes =  difference/60;
        size_t seconds = 0;
        if(minutes != 0){
            seconds = difference - 60*(minutes);
        }else{
            seconds = difference;
        }
        if(D)fprintf(E, "DEBUG: minutes = %zu\n", minutes);
        if(D)fprintf(E, "DEBUG: second = %zu\n", seconds);
        execution_time_to_string(info->time_str , 8, minutes, seconds);
        info->command = strdup(curr->command);
        print_process_info(info);
        free(info->start_str);
        info->start_str = NULL;
        free(info->time_str);
        info->time_str = NULL;
        free(info->command);
        info->command = NULL;
        vector_destroy(stats);
        sstring_destroy(sstr);

    }
}

int shell(int argc, char *argv[]) {
    parse_args(argc, argv);
    init();
    hist = history_create(path_history);

    // struct sigaction act;
    // memset(&act, 0, sizeof(act));
    // act.sa_handler = handle_EOF;

    signal(SIGINT, handle_sigint);


    if(flag_script) {
        execute_script(path_script);
        program_exit = 1;
    }

    char *cmd_buffer = NULL;
    size_t cmd_size = 0;
    ssize_t cmd_chars;
    // ---------------------------------------- SHELL READ IN HANDLE ----------------------------------------
    while( !program_exit && !program_interrupt) {
        print_prompt(cwd , getpid());
        cmd_chars = getline(&cmd_buffer, &cmd_size, stdin);

        if (cmd_chars > 0) {
            cmd_buffer[cmd_chars-1] = '\0';
        }


        if(D)fprintf(E,"DEBUG: ENTRY IS |%zd| of len: %zu\n", cmd_chars, strlen(cmd_buffer));
        if(cmd_chars == -1){
            break;
        }
        if(strlen(cmd_buffer) == 0){
            continue;
        }
        if(!strcmp(cmd_buffer, "exit")){
            break;
        } else {
            int exec_out = execute_command(cmd_buffer);  // Entry point for handling commands
            if(exec_out == 9){
                program_exit = 1;
            }
        }
 

    }
    if(cmd_buffer != NULL) {
        free(cmd_buffer);
    }
    handle_program_exit();
    // TODO: handle_program_exit
    return 0;
    
}  

char* remove_leading_whitespace(char* input){
    int length = strlen(input);
    char* output = calloc(strlen(input), sizeof(char) );
    if(input[0] == ' '){ 
        memmove(output, input+1, length - 1); 
        free(input);
        return output;
    } else{
        free(output);
        return input;
    }

}
int execute_chained_command(char* command, char* chain) {
        int output = 0; 
        sstring* temp_sstring = cstr_to_sstring(command);
        sstring_substitute(temp_sstring, 0, chain, "^");
        vector* temp_vector = sstring_split(temp_sstring, '^');
        char* command_1 = strdup(vector_get(temp_vector, 0));
        char* command_2 = strdup(vector_get(temp_vector, 1));
        char* trimmed_command_2 = remove_leading_whitespace(command_2);
        if(!strcmp("&&", chain)){
            output = execute_command(command_1);
            if(output == 0){
                output = execute_command(trimmed_command_2);
            }
        } else if(!strcmp("||", chain) ){
            output = execute_command(command_1);
            if(output != 0) {
                output = execute_command(trimmed_command_2);
                return output;
            }
        } else if(!strcmp(";", chain)){
            output = execute_command(command_1);
            output = execute_command(trimmed_command_2);
            return output;
        }
        return output;
}



// This will handle the logic for executing commands.
// That includes calling built-ins, and exec-ing
// TODO: Handle logic for chained commands!
// TODO: Add commands called to history
// TODO: Set up pgrid, backgrounding
// TODO: Set up signal handlers
// TODO: Handle chains
// This gonna be a big boi
// BUILT IS TO HANDLE:
//  cd <path>
// !history
// #index
// !prefix
// exit
// exec
int execute_command(char* command) {
    int cmd_out = 0;
    sstring* cmd_sstring;
    int diff;
    char* second;
    char* first = calloc(128, 1);
    int total = strlen(command);
    char* chained_eval_and = strstr(command, "&&");
    char* chained_eval_or = strstr(command, "||");
    char* chained_eval_semi = strstr(command, ";");
    if(D)fprintf(E, "DEBUG Command requested is: |%s|\n", command);
    if(chained_eval_or != NULL ) {
        diff = strlen(chained_eval_or);
        int actual = total - diff;
        second = strdup(chained_eval_or + 3);
        first = strncpy(first, command, actual-1);
        if(D)fprintf(E, "DEBUG First command is: |%s|\n", first);
        if(D)fprintf(E, "DEBUG Second is: |%s|\n", second);
        int result = execute_command(first);
        if(result != 0){
            result = execute_command(second);
        }
        free(first);
        first = NULL;
        free(second);
        second = NULL;
        return result;

    } else if (chained_eval_and != NULL ) {
        diff = strlen(chained_eval_and);
        int actual = total - diff;
        second = strdup(chained_eval_and + 3);
        first = strncpy(first, command, actual-1);
        if(D)fprintf(E, "DEBUG First command is: |%s|\n", first);
        if(D)fprintf(E, "DEBUG Second is: |%s|\n", second);
        int result = execute_command(first);
        if(result == 0){
            result = execute_command(second);
        }
        free(first);
        first = NULL;
        free(second);
        second = NULL;
        return result;

    } else if(chained_eval_semi != NULL) {
        diff = strlen(chained_eval_semi);
        int actual = total - diff;
        second = strdup(chained_eval_semi + 2);
        first = strncpy(first, command, actual-1);
        if(D)fprintf(E, "DEBUG First command is: |%s|\n", first);
        if(D)fprintf(E, "DEBUG Second is: |%s|\n", second);
        int result = execute_command(first);
        result = execute_command(second);
        free(first);
        first = NULL;
        free(second);
        second = NULL;
        return result;
    }


    bool flag_background = 0;
    if(command[strlen(command)-1] == '&' || command[strlen(command)-2] == '&'){
        flag_background = 1;
        sstring* temp_sstring = cstr_to_sstring(command);
        sstring_substitute(temp_sstring, 0, "&", "\0");
        char* background = sstring_to_cstr(temp_sstring);
        free(temp_sstring);
        cmd_sstring = cstr_to_sstring(background);
    } else {
        cmd_sstring = cstr_to_sstring(command);
    }

    vector* cmd_vec = string_vector_create();
    cmd_vec = sstring_split(cmd_sstring, ' ');
    if(!strcmp(vector_get(cmd_vec, 0), "ps")){
        vector_push_back(hist->history_vector,  command);
        print_processes();
        return 0;
    }else if(!strcmp(vector_get(cmd_vec, 0), "cd")){
        vector_push_back(hist->history_vector,  command);
        cmd_out = change_directory(vector_get(cmd_vec, 1));
        return cmd_out;
    } else if(!strcmp(vector_get(cmd_vec, 0), "!history")){
        history_print(hist);
        return 0;
    } else if(!strncmp(command, "#", 1)) {
        sstring_substitute(cmd_sstring, 0, "#", "");
        char* temp_cstr = sstring_to_cstr(cmd_sstring);
        cmd_out = history_index(hist, atoi(temp_cstr)); // ASK: IS THIS NOT AN INT?
        return cmd_out;
    } else if(!strncmp(command, "!", 1)) {
        sstring_substitute(cmd_sstring, 0, "!", "");
        char* temp_cstr = sstring_to_cstr(cmd_sstring);
        cmd_out = history_prefix(hist, temp_cstr);
        return cmd_out;
    } else if(!strncmp(command, "kill", 4)) {
        sstring_substitute(cmd_sstring, 0, "kill ", "");
        char* temp_cstr = sstring_to_cstr(cmd_sstring);
        int temp_p = atoi(temp_cstr);    
        if(command[strlen(command)-1] == 'l'){
            print_invalid_command(command);
        } else{
            kill_proc(temp_p);
        }
    } else if(!strncmp(command, "stop", 4)) {
        sstring_substitute(cmd_sstring, 0, "stop ", "");
        char* temp_cstr = sstring_to_cstr(cmd_sstring);
        int temp_p = atoi(temp_cstr);
        if(command[strlen(command)-1] == 'p'){
            print_invalid_command(command);
        } else{
            stop_proc(temp_p);
        }
    } else if(!strncmp(command, "cont", 4)) {
        sstring_substitute(cmd_sstring, 0, "cont ", "");
        char* temp_cstr = sstring_to_cstr(cmd_sstring);
        int temp_p = atoi(temp_cstr); 
        if(command[strlen(command)-1] == 't'){
            print_invalid_command(command);
        } else{
            cont_proc(temp_p);
        }
    } else if(!strcmp(command, "exit")) {
        return 9; // ASK : Do we need to deal with chains here?

        
    } else {
        // ---------------------------------------- THIS IS THE LOGIC FOR FORKING ----------------------------------------
        // ASK: Do I need to setup process info?
        // Create an char* argv[]
        vector_push_back(hist->history_vector, command);
        size_t cmd_len = vector_size(cmd_vec); // ASK: Will this segfault with null input?
        char **temp_argv = malloc(sizeof(char*)*cmd_len + 1); // ASK: Will this be off by 1 do to null pointer?
        for(size_t i = 0; i < vector_size(cmd_vec); i++) {
            //size_t temp_len = strlen(vector_get(cmd_vec, i));
            if(!strcmp(vector_get(cmd_vec, i),"")){
                continue;
            }
            temp_argv[i] = strdup(vector_get(cmd_vec, i)); // ASK: Do you want strdup or strcpy?
        }
        temp_argv[vector_size(cmd_vec)] = NULL;

        // Arguments have been constructed! Let's fork!
        fflush(stdout);
        pid_t child = fork();
        if (child == -1) { 
            // Fork failure
            print_fork_failed();
            return -1;
        } else if (child > 0) {
            if (setpgid(child, child) == -1) {
                print_setpgid_failed();
                exit(1);
            }

            time_t now;
            time(&now);
            process* current = malloc(sizeof(process*));
            current->command = strdup(command);
            current->pid = child;
            current->start = now;
            current->time = malloc(sizeof(struct tm));
            current->time = localtime(&now);

            if(D)fprintf(E, "DEBUG pushing pid: %d for command %s\n", child, command);
            vector_push_back(process_vec, current);

            if(!flag_background){
                flag_background = 0;
                int status;
                
                pid_t pid = waitpid(child, &status, 0);
                if (pid != -1 && WIFEXITED(status)) {
                    int low8bits = WEXITSTATUS(status);
                    if(D)fprintf(E,"DEBUG Process %d returned %d...\nDEBUG VECTOR...\n" , pid, low8bits);
                    // if(D){
                    //     for(size_t i = 0; i < vector_size(process_vec); i++) {
                    //         fprintf(E, "VEC: %zu -> |%s|\n", i, vector_get(process_vec, i));
                    //     }
                    // }
                    vector_pop_back(process_vec); // ASK: Do we need this to pop of done processes?
                    return low8bits;
                } else if( pid == -1) {
                    print_wait_failed();
                    return -1;
                }
            } 
            return 0;
        } else { 
            print_command_executed(getpid());
            if(D){
                int count = 0;
                while(*(temp_argv+count) != NULL){
                    fprintf(E,"DEBUG Entry is: |%s|...\n", *(temp_argv+count) );
                    count++;
                    if(count > 100){
                        break;
                    }
                }
            }
            int exec_status = execvp(temp_argv[0], temp_argv); // ASK: We don't need to eval return right?
            flag_background = 0;
            if(exec_status == -1){
                print_exec_failed(command);
                exit(1);
            }
        }

        // ---------------------------------------- END OF THE LOGIC FOR FORKING ----------------------------------------

    }
    return cmd_out;

}


void execute_script(const char* file_path) {
    vector* cmd_vec = string_vector_create();
    // NOTE: The contents of this function are based off examples in the man pages for getline and the coursebook
    // TODO: Put in handler for commands that take signals or exit
    // TODO: Print when file DNE
    FILE *script;
    char *buffer = NULL;
    size_t length = 0;
    ssize_t nchars;
    script = fopen(file_path, "r");
    if (script == NULL) {
        print_script_file_error();
        return;
    }

    while ( (nchars = getline(&buffer, &length, script)) != -1 ) {
        if (nchars > 0 && buffer[nchars-1] == '\n') {
            buffer[nchars-1] = '\0';
            vector_push_back(cmd_vec, buffer);
        }
    }

    free(buffer);
    fclose(script);


    // ASK DO WE NEED TO HANDLE EXIT?
    for(size_t i = 0; i < vector_size(cmd_vec); i++){
        print_prompt(cwd, getpid());
        print_command(vector_get(cmd_vec, i));
        //int cmd_out = 
        execute_command(vector_get(cmd_vec, i));
        // if(cmd_out == 9){
        //     break;
        // }
    }

    // if (get_pid() == base->pid) {

    // }
    vector_destroy(cmd_vec);
    return;
    // After this, we return to shell and handle program exit

}

void handle_program_exit() {
    if(hist->history_path != NULL){
        history_write(hist);
        free(hist->history_path);
    }
    vector_destroy(hist->history_vector);
    free(hist);
    //free(cwd);
    vector_destroy(process_vec);
}

int change_directory(const char* new_dir) {
    // ASK: How does this handle absolute paths?
    int out = chdir(new_dir);
    if (out == -1){
        print_no_directory(new_dir);
        return -1;
    } else {
       getcwd(cwd, sizeof(cwd));
       return 0;
    }

}


history* history_create(const char* file_path) { 
    history *this = (history*) malloc(sizeof(history));
    this->history_vector = string_vector_create();
    if(file_path == NULL) {
        this->history_path = NULL;
    } else {
        this->history_path = strdup(file_path);
        int check_exist = access(this->history_path, F_OK);
        FILE* backlog = fopen(this->history_path, "a+");
        if(check_exist == 0){
            char *cmd_buffer = NULL;
            size_t cmd_size = 0;
            ssize_t cmd_chars;
            cmd_chars = getline(&cmd_buffer, &cmd_size, backlog);
            while(cmd_chars != -1){
                if (cmd_chars > 0 && cmd_buffer[cmd_chars-1] == '\n') {
                    cmd_buffer[cmd_chars-1] = '\0';
                }
                vector_push_back(this->history_vector, cmd_buffer);
                cmd_chars = getline(&cmd_buffer, &cmd_size, backlog);
            }
        }else{
            print_history_file_error();
        }
        int local_fd = fileno(backlog);
        this->history_handle = local_fd;
        fclose(backlog); // ASK: Will this cause the program to decay?
    }
    return this;
}

void history_write(history *this) {
    // TODO: Print when file DNE
    // This is for seeing if the file previously exsists!
    FILE *test_file = fopen(this->history_path, "r");
    if(test_file ==  NULL){
        print_history_file_error();
    }
    fclose(test_file);

    FILE *output = fopen(this->history_path, "a+");
    size_t length = vector_size(this->history_vector);
    
    for(size_t i = 0; i < length; i++) {
        fprintf(output,"%s\n",vector_get(this->history_vector, i));
    }
    fclose(output);
}

void history_print(history *this)  {
    size_t length = vector_size(this->history_vector);
    for(size_t i = 0; i < length; i++) {
        print_history_line(i, vector_get(this->history_vector, i)); // ASK: Do we want print_history_line?
    }
}

int history_index(history *this, int index) {
    int length = vector_size(this->history_vector);
    if((index < 0) || (index >= length)) {
        print_invalid_index();
        return -1;
    } else {
        print_command(vector_get(this->history_vector, index)); 
        execute_command(vector_get(this->history_vector, index));
        return 0;
    }
}

int history_prefix(history *this, const char* prefix) {
    int length = vector_size(this->history_vector);

    if(prefix == '\0') {
        size_t length = vector_size(this->history_vector);
        print_command(vector_get(this->history_vector, length - 1)); 
        execute_command(vector_get(this->history_vector, length - 1));
        return 0;
    }

    for(int i = (length - 1); i >= 0; i--) {
        int cmp = strncmp(vector_get(this->history_vector, i), prefix, strlen(prefix));
        if(!cmp) {
            print_command(vector_get(this->history_vector, i)); 
            execute_command(vector_get(this->history_vector, i));
            return 0;
        }
    }
    // If we get here no matches were found!
    print_no_history_match();
    return -1;

}
