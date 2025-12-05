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

void screen_refresh(board_t * game_board, int mode) {
    debug("REFRESH\n");
    draw_board(game_board, mode);
    refresh_screen();
    if(game_board->tempo != 0)
        sleep_ms(game_board->tempo);       
}


int checkpoint_save(int *n_checkpoints) {
    terminal_cleanup();
    int pid = fork();
    if (pid == 0){
        (*n_checkpoints)++;
        terminal_init();
    }
    else if (pid > 0){
        int status;
        wait(&status);
        if (WIFEXITED(status)) {
            int son_end_state = WEXITSTATUS(status);
            if (son_end_state == GAME_WON) {
                return GAME_WON;
            }
        } 
        terminal_init();
    }
    return CONTINUE_PLAY;
}

int no_checkpoints(int *n_checkpoints) {
    return (*n_checkpoints) == 0;
}

int play_board(board_t * game_board, int *checkpoints) {
    pacman_t* pacman = &game_board->pacmans[0];  //ELE SO MEXE NO PACMAN 0!!!!!!!
    command_t* play;
    if (pacman->n_moves == 0) { // if is user input
        command_t c; 
        c.command = get_input();

        if(c.command == '\0')
            return CONTINUE_PLAY;

        c.turns = 1;
        play = &c;
    }
    else { // else if the moves are pre-defined in the file
        // avoid buffer overflow wrapping around with modulo of n_moves
        // this ensures that we always access a valid move for the pacman
        play = &pacman->moves[pacman->current_move%pacman->n_moves];
    }

    debug("KEY %c\n", play->command);

    if(play->command ==  'G'){
        if (no_checkpoints(checkpoints)){
            return CREATE_BACKUP;
        }
        return CONTINUE_PLAY;
    }

    if (play->command == 'Q') {
        return QUIT_GAME;
    }

    int result = move_pacman(game_board, 0, play);
    if (result == REACHED_PORTAL) {
        // Next level
        return NEXT_LEVEL;
    }

    if(result == DEAD_PACMAN) {
        if (no_checkpoints(checkpoints)){
            return QUIT_GAME;
        }
        return LOAD_BACKUP;
    }
    
    for (int i = 0; i < game_board->n_ghosts; i++) {
        ghost_t* ghost = &game_board->ghosts[i];
        // avoid buffer overflow wrapping around with modulo of n_moves
        // this ensures that we always access a valid move for the ghost
        move_ghost(game_board, i, &ghost->moves[ghost->current_move%ghost->n_moves]);
    }

    if (!game_board->pacmans[0].alive) {
        return QUIT_GAME;
    }      

    return CONTINUE_PLAY;  
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

/*int position(int row, int column, int width){
    return row * width + column;
}*/

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
        //if (strncmp(buffer, "DIM", 3) == 0){  ###
        if (strncmp(buffer, "DIM", 3) == 0){
            sscanf(buffer, "DIM %d %d\n", &new_level->width, &new_level->height);
            new_level->board = malloc(sizeof(board_pos_t) * new_level->height * new_level->width);
            //printf("DIM %d %d\n", new_level->width, new_level->height);
        }
        
        else if (strncmp(buffer, "TEMPO", 5) == 0){
            sscanf(buffer, "TEMPO %d\n", &new_level->tempo);
            //printf("TEMPO %d\n", new_level->tempo);
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
            //printf("pos monster pre board\n");
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

                //printf("%c", new_level->board[position(row, column, new_level->width)].content);
                
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


int main(int argc, char** argv) {
    board_t* levels = NULL;
    int accumulated_points = 0;
    bool end_game = false;
    int curr_lvl = 0;

    board_t game_board;

    DIR *dir;
    struct dirent *file_entry;

    int n_levels = 0, checkpoints = 0;

    char* extension;

    int end_state = CONTINUE_PLAY;
    int draw_state = DRAW_MENU;
    

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
            }/*
            char file_path[512];
            board_t new_level = {0};

            build_filepath(file_path, argv[1], file_entry->d_name);

            create_level(&new_level, argv[1], file_path);

            put_creatures_on_board(&new_level);

            strcpy(new_level.level_name, file_entry->d_name); //copia o nome do ficheiro para a estrutura do board
            levels = realloc(levels, (n_levels + 1) * sizeof(board_t)); //realoca o array de strings para adicionar mais um nivel

            levels[(n_levels)++] = new_level; //adiciona o nome ao array de strings
            */
        }
    }




    closedir(dir);

    while (!end_game) {
        game_board = levels[curr_lvl];
        game_board.pacmans[0].points = accumulated_points;
        //game_board.checkpoints = checkpoints;
        draw_board(&game_board, DRAW_MENU);
        refresh_screen();

        while(true) {
            int result = play_board(&game_board, &checkpoints); 

            if(result == NEXT_LEVEL) {
                sleep_ms(game_board.tempo);
                curr_lvl++;
                if (curr_lvl >= n_levels) {
                    end_game = true; //se acabarem os levels no filho, o pai tbm precisa acabar
                    end_state = GAME_WON;
                    draw_state = DRAW_WIN;
                    screen_refresh(&game_board, draw_state);
                }
                break;
            }

            if(result == QUIT_GAME) {
                draw_state = DRAW_GAME_OVER;
                end_game = true;
                end_state = QUIT_GAME;
                if (no_checkpoints(&checkpoints)){
                    sleep_ms(game_board.tempo);
                    screen_refresh(&game_board, DRAW_GAME_OVER);
                }
                break;
            }

            if (result == CREATE_BACKUP){
                if (checkpoint_save(&checkpoints) == GAME_WON){
                    end_game = true;
                    end_state = GAME_WON;
                }
                break;
            }
            if (result == LOAD_BACKUP){
                draw_state = DRAW_GAME_OVER;
                sleep_ms(game_board.tempo);
                end_game = true;
                end_state = LOAD_BACKUP;
                break;
            }
    
            screen_refresh(&game_board, draw_state); 

            accumulated_points = game_board.pacmans[0].points;      
        }
        print_board(&game_board);
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