#include "board.h"
#include "display.h"
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>

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

int play_board(board_t * game_board) {
    pacman_t* pacman = &game_board->pacmans[0];
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

    if (play->command == 'Q') {
        return QUIT_GAME;
    }

    int result = move_pacman(game_board, 0, play);
    if (result == REACHED_PORTAL) {
        // Next level
        return NEXT_LEVEL;
    }

    if(result == DEAD_PACMAN) {
        return QUIT_GAME;
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

int read_line(int file,char* buffer){
    char c;
    int i = 0;
    while(read(file,&c,1) != NULL && c != '\n'){
        strcat(buffer, &c);
        i++;
    }
    strcat(buffer,'\0');
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

int main(int argc, char** argv) {
    char buffer[4096];
    char** levels;
    int accumulated_points = 0;
    bool end_game = false;

    board_t game_board;
    board_t new_board;

    DIR *dir;
    struct dirent *entry;

    int n_levels, n_monsters, n_pacmans = 0;

    char* extension;

    int file;
    char file_path[512];

    int ghost = 0;

    pacman_t* pacman;

    if (argc != 2) {
        printf("Usage: %s <level_directory>\n", argv[0]);
        // TODO receive inputs
    }

    // Random seed for any random movements
    srand((unsigned int)time(NULL));

    open_debug_file("debug.log");

    terminal_init();

    new_board = malloc(sizeof(board_t));

    dir = opendir(argv[1]);

    if (dir == NULL) {
        perror("opendir");
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (entry < 0) {
            perror("readdir");
            closedir(dir);
            return -1;
        }
        // ler o nome dos ficheiros
        if  (special_directory(entry->d_name)) {
            continue; // skip . and ..
        }
        new_board = malloc(sizeof(board_t));
        extension = strrchr(entry->d_name, '.'); //obtem a extensao do ficheiro

        if (extension == NULL){
            continue;
        }

        else if(strcmp(extension,".lvl") == 0){
            n_levels++;

            build_filepath(&file_path, argv[1], entry->d_name);

            file = open(file_path, "O_RDONLY");

            if(file < 0){
                perror("open");
                closedir(dir);
                return -1;
            }

            while (read_line(file, buffer) != 0){

                if (strncmp(buffer, "DIM", 3) == 0){
                    sscanf(buffer, "DIM %d %d\n", &new_board->width, &new_board->height);
                }
                else if (strncmp(buffer, "TEMPO", 5) == 0){
                    sscanf(buffer, "TEMPO %d\n", &new_board->tempo);
                }
                else if (strncmp(buffer, "PAC", 3) == 0){
                    char pacman[256];

                    for (int i = 4; buffer[i] != '\n'; i += 3){
                        sscanf(buffer + i, " %s.p", pacman);

                        build_filepath(&file_path, argv[1], pacman);

                        pacman = malloc(sizeof(pacman_t));
                        
                        //strcpy(new_board->pacman_file, file_path); // agora basicamente falta ler os fantasmas e os pacs para o board
                        create_creature(&pacman, file_path); // agora basicamente falta ler os fantasmas e os pacs para o board

                        new_board->pacmans[n_pacmans++] = pacman;
                    }
                    new_board->n_pacmans = n_pacmans;
                }
            }
            

            //strcpy(new_board->level_name, entry->d_name); //copia o nome do ficheiro para a estrutura do board

            levels[n_levels-1]= new_board; //adiciona o nome ao array de strings
        }
    }

// Main game loop - precisa da lista de niveis e etc

    

    while (!end_game) {
        load_level(&game_board, accumulated_points);
        draw_board(&game_board, DRAW_MENU);
        refresh_screen();

        while(true) {
            int result = play_board(&game_board); 

            if(result == NEXT_LEVEL) {
                screen_refresh(&game_board, DRAW_WIN);
                sleep_ms(game_board.tempo);
                break;
            }

            if(result == QUIT_GAME) {
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

    terminal_cleanup();

    close_debug_file();

    return 0;
}
