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

void screen_refresh(board_t * game_board, int mode) {
    debug("REFRESH\n");
    draw_board(game_board, mode);
    refresh_screen();
    if(game_board->tempo != 0)
        sleep_ms(game_board->tempo);       
}

bool no_checkpoints(board_t *game_board){
    return game_board->checkpoints == 0;
}

void checkpoint_save(board_t *game_board) {
    terminal_cleanup();
    int pid = fork();
    if (pid == 0){
        game_board->checkpoints += 1;
        terminal_init();
    }
    else if (pid > 0){
        int status;
        wait(&status);
        terminal_init();
    }
}

int play_board(board_t * game_board) {
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
        if (no_checkpoints(game_board)){
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
        if (!no_checkpoints(game_board)){
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
    while (read(file, &c, 1) > 0 && c != '\n') {
        buffer[i++] = c;
    }

    buffer[i] = '\0';
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

int create_level(board_t* new_board, char* level_file_path){
    char buffer[4096];

    char pacman_f[256];
    char* next_pacman;
    pacman_t* pacman;

    char monster_f[256];
    char* next_monster;
    ghost_t* monster;

    int char_read = 0;

    int column = 0, row = 0;

    bool no_pacman = true;

    int level_file = open(level_file_path, O_RDONLY);

    if(level_file < 0){
        return -1;
    }

    while (read_line(level_file, buffer) != 0){
        if (strncmp(buffer, "DIM", 3) == 0){
            sscanf(buffer, "DIM %d %d\n", &new_board->width, &new_board->height);
            new_board->board = malloc(sizeof(board_pos_t) * new_board->height * new_board->width);
        }
        else if (strncmp(buffer, "TEMPO", 5) == 0){
            sscanf(buffer, "TEMPO %d\n", &new_board->tempo);
        }
        else if (strncmp(buffer, "PAC", 3) == 0){
            char_read = 0;
            next_pacman = buffer + 4;

            no_pacman = false;

            while(buffer[char_read] != '\0' && buffer[char_read] != '\n'){
                next_pacman += char_read;

                sscanf(next_pacman, "%[^.].p%n", pacman_f, &char_read);
                strcat(pacman_f, ".p");
                build_filepath(file_path, argv[1], pacman_f);

                pacman = (pacman_t*) malloc(sizeof(pacman_t));
                
                load_pacman(pacman, file_path);

                new_board->pacmans = realloc(new_board->pacmans, sizeof(pacman_t*) * (new_board->n_pacmans + 1));

                strcpy(new_board->pacman_file, pacman_f);
                new_board->pacmans[new_board->n_pacmans++] = pacman;
            }
        }
        else if (strncmp(buffer, "MON", 3) == 0){
            char_read = 0;
            next_monster = buffer + 4;
            while(buffer[char_read] != '\0' && buffer[char_read] != '\n'){
                next_monster += char_read;

                sscanf(next_monster, "%[^.].m%n", monster_f, &char_read);
                strcat(monster_f, ".m");
                build_filepath(file_path, argv[1], monster_f);

                monster = (ghost_t*) malloc(sizeof(ghost_t));
                
                load_monster(monster, file_path); 

                new_board->ghosts = realloc(new_board->ghosts, sizeof(ghost_t*) * (new_board->n_ghosts + 1));

                strcpy(new_board->ghosts_files[new_board->n_ghosts], monster_f);
                new_board->ghosts[new_board->n_ghosts++] = *monster;
            }  
        }
        else if (strncmp(buffer, "X", 1) == 0 || strncmp(buffer, "o", 1) == 0 || strncmp(buffer, "@", 1) == 0){
            while(buffer[column] != '\0' && buffer[column] != '\n'){

                new_board->board[position(row, column, new_board->width)].content = buffer[column];
                
                if (no_pacman && buffer[column] == 'o'){
                    pacman = malloc(sizeof(pacman_t));

                    pacman->pos_x = column;
                    pacman->pos_y = row;
                    pacman->alive = 1;

                    new_board->n_pacmans++;
                    new_board->pacmans[0] = pacman;

                    no_pacman = false;
                }
                column++;
            }

            row++;
        }
    }
    
    put_creatures_on_board(new_board,free);

    strcpy(new_board->level_name, file_entry->d_name); //copia o nome do ficheiro para a estrutura do board

    levels = realloc(levels, n_levels * sizeof(board_t*)); //realoca o array de strings para adicionar mais um nivel

    levels[n_levels-1]= new_board; //adiciona o nome ao array de strings

    return 0;
}


int main(int argc, char** argv) {
    char buffer[4096];
    board_t** levels = NULL;
    int accumulated_points = 0;
    bool end_game = false;

    board_t game_board;
    board_t* new_board;

    DIR *dir;
    struct dirent *file_entry;

    int n_levels = 0, n_monsters = 0, n_pacmans = 0;

    char* extension;

    int file;
    char file_path[512];

    pacman_t* pacman;
    ghost_t* monster;

    int row = 0;
    int free = 0;

    /*---------------------------------------------------------------------*/

    if (argc != 2) {
        printf("Usage: %s <level_directory>\n", argv[0]);
        // TODO receive inputs
    }

    // Random seed for any random movements
    srand((unsigned int)time(NULL));

    open_debug_file("debug.log");

    terminal_init();

    //new_board = malloc(sizeof(board_t));

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

        new_board = malloc(sizeof(board_t));
        extension = strrchr(file_entry->d_name, '.'); //obtem a extensao do ficheiro

        if (extension == NULL){
            continue;
        }
        else if(strcmp(extension,".lvl") == 0){
            build_filepath(file_path, argv[1], file_entry->d_name);

            new_board = (board_t*) malloc(sizeof(board_t));

            if(create_level(&new_board, file_path) < 0){
                perror("open");
                closedir(dir);
                return -1;
    }

            put_creatures_on_board(new_board);

            strcpy(new_board->level_name, file_entry->d_name); //copia o nome do ficheiro para a estrutura do board

            levels = realloc(levels, (n_levels + 1) * sizeof(board_t*)); //realoca o array de strings para adicionar mais um nivel

            levels[n_levels++] = new_board; //adiciona o nome ao array de strings
        }
    }

// Main game loop - precisa da lista de niveis e etc

    close(dir);

    while (!end_game) {
        int lvl = 0;
        levels[lvl]->pacmans[0].points = accumulated_points;
        draw_board(levels[lvl], DRAW_MENU);
        refresh_screen();

        while(true) {
            int result = play_board(&game_board); 

            if(result == NEXT_LEVEL) {
                screen_refresh(&game_board, DRAW_WIN);
                sleep_ms(game_board.tempo);
                lvl++;
                if (lvl >= n_levels) {
                    end_game = true; //se acabarem os levels no filho, o pai tbm precisa acabar
                }
                break;
            }

            if(result == QUIT_GAME) {
                screen_refresh(&game_board, DRAW_GAME_OVER); 
                sleep_ms(game_board.tempo);
                end_game = true;
                break;
            }

            if (result == CREATE_BACKUP){
                checkpoint_save(&game_board);
                break;
            }
            if (result == LOAD_BACKUP){
                //lean_board_memory(game_board);
                //terminal_cleanup();
                screen_refresh(&game_board, DRAW_GAME_OVER); 
                sleep_ms(game_board.tempo);
                end_game = true;
                break;
            }
    
            screen_refresh(&game_board, DRAW_MENU); 

            accumulated_points = game_board.pacmans[0].points;      
        }
        print_board(&game_board);
        unload_level(&game_board);
    }    
    //clean_board_memory(game_board);

    terminal_cleanup();

    close_debug_file();

    return 0;
}