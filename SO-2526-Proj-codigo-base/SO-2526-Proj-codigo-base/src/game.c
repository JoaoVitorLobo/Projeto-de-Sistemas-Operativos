#include "board.h"
#include "display.h"
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <fcntl.h>
#include <sys/wait.h>


#define CONTINUE_PLAY 0
#define NEXT_LEVEL 1
#define QUIT_GAME 2
#define LOAD_BACKUP 3
#define CREATE_BACKUP 4
#define GAME_WON 5
#define QUIT_ALL_GAMES 6

void join_all_threads(board_t* game_board);

void screen_refresh(board_t* game_board, int mode) {
    pthread_mutex_lock(&game_board->ncurses_lock);
    draw_board(game_board, mode);
    refresh_screen();
    if(game_board->tempo != 0)
        sleep_ms(game_board->tempo);       
    pthread_mutex_unlock(&game_board->ncurses_lock);
}


int checkpoint_save(int *n_checkpoints,board_t* board) {
    terminal_cleanup();
    
    pthread_mutex_lock(&board->board_lock);
    board->running = 0; // threads morrem
    pthread_mutex_unlock(&board->board_lock);

    join_all_threads(board);

    int pid = fork();

    if (pid == 0){
        (*n_checkpoints) = 1;
        terminal_init();
        return CONTINUE_PLAY;
    }
    else if (pid > 0){
        (*n_checkpoints) = 0;
        int status;
        wait(&status);
        if (WIFEXITED(status)) {
            int son_end_state = WEXITSTATUS(status);
            if (son_end_state == GAME_WON) {
                return GAME_WON;
            }
            else if (son_end_state == QUIT_ALL_GAMES){
                return QUIT_GAME;
            }
        } 
        terminal_init();
    }
    return CONTINUE_PLAY;
}

int no_checkpoints(int *n_checkpoints) {
    return (*n_checkpoints) == 0;
}


int read_line(int file, char* buffer){
    char c;
    int i = 0;
    while (read(file, &c, 1) > 0 && c != '\n' && c!= '\0' && c != EOF) {
        buffer[i++] = c;
    }

    if(i > 0){
        buffer[i] = '\0';
    }
    return i;
}

bool special_directory(char *d_name){
    return  strcmp(d_name, ".") == 0 || strcmp(d_name, "..") == 0 ;
}

void build_filepath(char* file_path, char* dir_path, char* d_name){
    strcpy(file_path, dir_path);
    strcat(file_path, "/");
    strcat(file_path,d_name);
}

int create_level(board_t* new_level, char* directory,char* level_file_path){
    char buffer[4096];
    char file_path[512];  

    int column = 0, row = 0;

    bool no_pacman = true;

    int level_file = open(level_file_path, O_RDONLY);

    if(level_file < 0){
        return -1;
    }

    /* Ensure dynamic arrays and counters are in a known default state */
    new_level->board   = NULL;
    new_level->pacmans = NULL;
    new_level->ghosts  = NULL;
    new_level->n_pacmans = 0;
    new_level->n_ghosts  = 0;
    new_level->tempo = 0; 

    while (read_line(level_file, buffer) != 0){
        printf("LINE: \"%s\"\n", buffer);
        if (strncmp(buffer, "DIM", 3) == 0){
            sscanf(buffer, "DIM %d %d\n", &new_level->width, &new_level->height);
            new_level->board = malloc(sizeof(board_pos_t) * new_level->height * new_level->width);
        }
        
        else if (strncmp(buffer, "TEMPO", 5) == 0){
            sscanf(buffer, "TEMPO %d\n", &new_level->tempo);
        }

        else if (strncmp(buffer, "PAC", 3) == 0) {
            no_pacman = false;

            char* next_pacman = NULL;

            //alterar esse nome token
            char* token = strtok_r(buffer + 4, " \t\n", &next_pacman);

            while (token != NULL) {

                char pacman_f[256];
                snprintf(pacman_f, sizeof(pacman_f), "%s", token);

            
                printf("PACMAN FILENAME: %s\n", pacman_f);

                build_filepath(file_path, directory, pacman_f);

                pacman_t pacman = {0};

                load_pacman(&pacman, file_path);
                pacman.result = CONTINUE_PLAY;


                new_level->pacmans = realloc(new_level->pacmans, sizeof(pacman_t) * (new_level->n_pacmans + 1));
                strcpy(new_level->pacman_file, pacman_f);
                new_level->pacmans[new_level->n_pacmans++] = pacman;


                token = strtok_r(NULL, " \t\n", &next_pacman);
            }
        }

        else if (strncmp(buffer, "MON", 3) == 0){
            char* next_monster = NULL;

            char* token = strtok_r(buffer + 4, " \t\n", &next_monster);

            while(token != NULL){
                char monster_f[256];

                snprintf(monster_f, sizeof(monster_f), "%s", token);
            
                printf("MONSTER FILENAME: %s\n", monster_f);

                build_filepath(file_path, directory, monster_f);

                /* default-initialize all fields to zero/defaults */
                ghost_t monster = {0};
                
                load_monster(&monster, file_path); 

                new_level->ghosts = realloc(new_level->ghosts, sizeof(ghost_t) * (new_level->n_ghosts + 1));

                strcpy(new_level->ghosts_files[new_level->n_ghosts], monster_f);
                new_level->ghosts[new_level->n_ghosts++] = monster;

                token = strtok_r(NULL, " \t\n", &next_monster);
            }  
        }
        else if (strncmp(buffer, "X", 1) == 0 || strncmp(buffer, "o", 1) == 0 || strncmp(buffer, "@", 1) == 0){
            column = 0;
            while(buffer[column] != '\0' && buffer[column] != '\n'){
                switch (buffer[column]){

                
                case 'X':
                    new_level->board[position(row, column, new_level->width)].content = 'W';
                    break;
                case 'o':
                    new_level->board[position(row, column, new_level->width)].content = '.';
                    new_level->board[position(row, column, new_level->width)].has_dot = 1;
                    break;
                
                case '@':
                    new_level->board[position(row, column, new_level->width)].content = buffer[column];
                    new_level->board[position(row, column, new_level->width)].has_portal = 1;
                    break;
                default:
                    new_level->board[position(row, column, new_level->width)].content = buffer[column];
                }

                if (no_pacman && buffer[column] == 'o'){
                    pacman_t pacman = {0};

                    pacman.pos_x = column;
                    pacman.pos_y = row;
                    pacman.alive = 1;

                    /* append this pacman to the level's pacmans array */
                    new_level->pacmans = realloc(new_level->pacmans, sizeof(pacman_t) * (new_level->n_pacmans + 1));
                    new_level->pacmans[new_level->n_pacmans++] = pacman;

                    no_pacman = false;
                }
                column++;
            }
            //printf("==============\n");
            row++;    
        }
    }
    //printf("ultimo buffer: %s", buffer);
    new_level->g_threads = malloc(new_level->n_ghosts*sizeof(void*));
    return 0;
}

int parse_level(board_t** levels, int* n_levels, char* dir_path, char* level_file_name){
    char file_path[512];
    board_t new_level = {0};

    build_filepath(file_path, dir_path, level_file_name);

    create_level(&new_level, dir_path, file_path);

    put_creatures_on_board(&new_level);

    strcpy(new_level.level_name, level_file_name); //copia o nome do ficheiro para a estrutura do board

    *levels = realloc(*levels, (*n_levels + 1) * sizeof(board_t)); //realoca o array de strings para adicionar mais um nivel

    (*levels)[(*n_levels)++] = new_level; //adiciona o nome ao array de strings
    return 0;
}

void *pacman_thread_func(void *arg) {
    board_t* board = (board_t*)arg;
    pacman_t* pacman = &board->pacmans[0]; 

    command_t* play;

    while (board->running) {
        
        if (!pacman->alive){
            pthread_mutex_lock(&pacman->pacman_lock);
            pacman->result = QUIT_GAME;
            pthread_mutex_unlock(&pacman->pacman_lock);
            return NULL;
        }

        //get play
        
        if (pacman->n_moves == 0) { // if is user input
            command_t c; 
            c.command = get_input();

            if(c.command == '\0')
                continue;

            c.turns = 1;
            play = &c;
        }//running

        else if(pacman->n_moves > 0) {
            play = &pacman->moves[pacman->current_move%pacman->n_moves];
        }

        //get result of play

        pthread_mutex_lock(&pacman->pacman_lock);
        pacman->result = CONTINUE_PLAY;
        pthread_mutex_unlock(&pacman->pacman_lock);

        if (play->command ==  'G'){
            if (no_checkpoints(board->checkpoints)){
                board->running = 0;
                pthread_mutex_lock(&pacman->pacman_lock);
                pacman->result = CREATE_BACKUP;
                pthread_mutex_unlock(&pacman->pacman_lock);
                return NULL;            
            }
        }

        if (play->command == 'Q') {
            board->running = 0;
            pthread_mutex_lock(&pacman->pacman_lock);
            debug("play->command == 'Q'\n");
            pacman->result = QUIT_ALL_GAMES;
            pthread_mutex_unlock(&pacman->pacman_lock);
            return NULL;
        }

        //get result of move

        pthread_mutex_lock(&board->board_lock);
        pacman->result = move_pacman(board, 0, play);
        pthread_mutex_unlock(&board->board_lock);

        if (pacman->result == REACHED_PORTAL) {
            // Next level
            board->running = 0;
            pthread_mutex_lock(&pacman->pacman_lock);
            pacman->result = NEXT_LEVEL;
            pthread_mutex_unlock(&pacman->pacman_lock);
            return NULL;
        }

        if(pacman->result == DEAD_PACMAN) {
            board->running = 0;
            if (no_checkpoints(board->checkpoints)){
                pthread_mutex_lock(&pacman->pacman_lock);
                pacman->result = QUIT_GAME;
                pthread_mutex_unlock(&pacman->pacman_lock);
            }
            else{
                pthread_mutex_lock(&pacman->pacman_lock);
                pacman->result = LOAD_BACKUP;
                pthread_mutex_unlock(&pacman->pacman_lock);
            }
            return NULL;
        }

        //sleep_ms(board->tempo);
        
    }
    
    return NULL;
}

void *ghost_thread_func(void *arg) {
    ghost_thread* thread = (ghost_thread*) arg;
    int i = thread->id;
    int j = 0;
    while (thread->board->running) {
        j = (j + 1) % thread->board->ghosts[i].n_moves;
        pthread_mutex_lock(&thread->board->board_lock);
        move_ghost(thread->board, i, &thread->board->ghosts[i].moves[j]); // movimento dos ghosts é semelhante
        pthread_mutex_unlock(&thread->board->board_lock);
        sleep_ms(thread->board->tempo);
    }
    return NULL;
}

void *screen_refresh_thread(void *arg){
    board_t* board = (board_t*) arg;
    while(board->running){
        screen_refresh(board, DRAW_MENU);
        sleep_ms(board->tempo);
    }
    return NULL;
}

void innit_board_mutexes(board_t* board){
    pthread_mutex_init(&board->board_lock,NULL);
    pthread_mutex_init(&board->ncurses_lock,NULL);
}

void create_ghosts_threads(board_t* board){
    for(int i = 0; i < board->n_ghosts; i++) {
        ghost_thread* g_thread = malloc(sizeof(ghost_thread));
        g_thread->board = board;
        g_thread->id = i;
        board->g_threads[i] = g_thread;
        pthread_create(&board->ghosts[i].ghost_thread, NULL, ghost_thread_func,g_thread);
    } 
}

void create_pacman_thread(board_t* board){
    pthread_create(&board->pacmans[0].pacman_thread, NULL, pacman_thread_func, board);
}

void create_board_thread(board_t* board){
    pthread_create(&board->board_thread,NULL,screen_refresh_thread,board);
}

void join_all_ghosts_threads(board_t* board){
    for (int i = 0; i < board->n_ghosts; i++) {
        pthread_join(board->ghosts[i].ghost_thread, NULL);
    }
}

void join_pacmans_threads(board_t* board){
    pthread_join(board->pacmans[0].pacman_thread, NULL);
}

void join_board_threads(board_t* board){
   pthread_join(board->board_thread, NULL);
}

void join_all_threads(board_t* game_board){
    join_all_ghosts_threads(game_board);
    join_pacmans_threads(game_board);
    join_board_threads(game_board);
}

void handle_next_level(board_t* game_board, int* curr_lvl, int* n_levels, bool* end_game, int* end_state, int* accumulated_points){
    (*curr_lvl)++;
    if (*curr_lvl >= *n_levels) {
        screen_refresh(game_board, DRAW_WIN);
        *end_game = true;
        *end_state = GAME_WON;
        game_board->draw_state = DRAW_WIN;
        game_board->running = 0;   
    }
    *accumulated_points = game_board->pacmans[0].points;  
}

void handle_quit_game(board_t* game_board, bool* end_game, int* end_state, int* accumulated_points){
    game_board->draw_state = DRAW_GAME_OVER;
    *end_game = true;
    *end_state = QUIT_GAME;
    if (no_checkpoints(game_board->checkpoints)){
        sleep_ms(game_board->tempo);
        screen_refresh(game_board, DRAW_GAME_OVER);
    }
    game_board->running = 0; 
    *accumulated_points = game_board->pacmans[0].points; 
}

void handle_create_backup(board_t* game_board, bool* end_game, int* end_state, int* accumulated_points){
    int checkpoint_result = checkpoint_save(game_board->checkpoints,game_board);
    if (checkpoint_result == GAME_WON){
        *end_game = true;
        *end_state = GAME_WON;
    }
    else if (checkpoint_result == QUIT_GAME){
        *end_game = true;
        *end_state = QUIT_GAME;
    }
    game_board->running = 0; 
    *accumulated_points = game_board->pacmans[0].points;   
}

void handle_load_backup(board_t* game_board, bool* end_game, int* end_state, int* accumulated_points){
    *end_game = true;
    game_board->draw_state = DRAW_GAME_OVER;
    (*game_board->checkpoints) = 0;
    *end_state = LOAD_BACKUP;

    game_board->running = 0; 
    *accumulated_points = game_board->pacmans[0].points;  
}

void handle_quit_all_games(board_t* game_board, bool* end_game, int* end_state, int* accumulated_points){
    game_board->draw_state = DRAW_GAME_OVER;
    *end_game = true;
    *end_state = QUIT_ALL_GAMES;
    if (no_checkpoints(game_board->checkpoints)){
        sleep_ms(game_board->tempo);
        screen_refresh(game_board, DRAW_GAME_OVER);
    }
    game_board->running = 0; 
    *accumulated_points = game_board->pacmans[0].points; 
}

int main(int argc, char** argv) {
    board_t* levels = NULL;
    int accumulated_points = 0;
    bool end_game = false;
    int curr_lvl = 0;

    board_t* game_board;

    DIR *dir;
    struct dirent *file_entry;

    int n_levels = 0, checkpoints = 0;

    char* extension;

    int end_state = CONTINUE_PLAY;
    

    if (argc != 2) {
        printf("Usage: %s <level_directory>\n", argv[0]);
        return -1;
    }

    // Random seed for any random movements
    srand((unsigned int)time(NULL));

    open_debug_file("debug.log");

    //printf("Initializing terminal...\n");

    terminal_init(); 

    dir = opendir(argv[1]);

    if (dir == NULL) {
        perror("opendir");
        return -1;
    }

    /*----------------------------------------------------------------------*/

    while ((file_entry = readdir(dir)) != NULL) {
        // ler o nome dos ficheiros
        if  (special_directory(file_entry->d_name)) {
            continue; // skip . and ..
        }

        extension = strrchr(file_entry->d_name, '.'); //obtem a extensao do ficheiro

        if (extension == NULL){
            continue;
        }
        else if(strcmp(extension,".lvl") == 0){
            if(parse_level(&levels, &n_levels, argv[1], file_entry->d_name) < 0){
                perror("open");
                closedir(dir);
                return -1;
            }
        }
    }

    /*------------------------------------------------------------------*/


    closedir(dir);

    while (!end_game) {
        game_board = &levels[curr_lvl];
        game_board->pacmans[0].points = accumulated_points;
        game_board->running = 1;
        game_board->checkpoints = &checkpoints;
        game_board->draw_state = DRAW_MENU;

        innit_board_mutexes(game_board);

        draw_board(game_board, game_board->draw_state);

        pthread_mutex_lock(&game_board->ncurses_lock);
        refresh_screen();
        pthread_mutex_unlock(&game_board->ncurses_lock);

        while(true) {
            create_ghosts_threads(game_board);
            create_pacman_thread(game_board);
            create_board_thread(game_board);

            join_all_threads(game_board);

            int result = game_board->pacmans[0].result;

            if(result == NEXT_LEVEL) {
                handle_next_level(game_board, &curr_lvl, &n_levels, &end_game, &end_state, &accumulated_points);
                break;
            } 

            if(result == QUIT_GAME) {
                handle_quit_game(game_board, &end_game, &end_state, &accumulated_points);
                break;
            }

            if (result == CREATE_BACKUP){
                handle_create_backup(game_board, &end_game, &end_state, &accumulated_points);
                break;
            }
            if (result == LOAD_BACKUP){
                handle_load_backup(game_board, &end_game, &end_state, &accumulated_points);
                break;
            }
            if (result == QUIT_ALL_GAMES){
                handle_quit_all_games(game_board, &end_game, &end_state, &accumulated_points);
                break;
            }
            game_board->pacmans[0].result = CONTINUE_PLAY;
            accumulated_points = game_board->pacmans[0].points;      
        }

        game_board->running = 0; // nao tenho a certeza se deveria ser aqui ou depois do print board

        
        pthread_mutex_unlock(&game_board->board_lock);
        

    }    

    while (curr_lvl>=0 && curr_lvl<n_levels){

        for (int i= 0;i<levels[curr_lvl].n_ghosts;i++){
            free(levels[curr_lvl].g_threads[i]);
        }
        free(levels[curr_lvl].g_threads);
        curr_lvl--;
    }

    for (int i = 0; i < n_levels; i++) {
        unload_level(&levels[i]);
    }
    free(levels);

    sleep_ms(1000); // wait a bit before closing to see final state

    terminal_cleanup();

    close_debug_file();

    return end_state;
}