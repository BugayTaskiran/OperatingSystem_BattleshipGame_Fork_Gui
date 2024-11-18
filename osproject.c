#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <sys/wait.h>

#define GRID_SIZE 8
#define SHIP_TYPES 3
#define MAX_POTENTIAL_TARGETS 100
#define SAVE_FILE "battleship_save.dat"
#define AUTOSAVE_INTERVAL 5

typedef struct
{
    int size;
    int count;
    char symbol;
} Ship;

typedef struct
{
    int row;
    int col;
} Position;

typedef struct
{
    char grid1[GRID_SIZE][GRID_SIZE];
    char grid2[GRID_SIZE][GRID_SIZE];
    char ai1_knowledge[GRID_SIZE][GRID_SIZE];
    char ai2_knowledge[GRID_SIZE][GRID_SIZE];
    Position ai1_targets[MAX_POTENTIAL_TARGETS];
    Position ai2_targets[MAX_POTENTIAL_TARGETS];
    int ai1_num_targets;
    int ai2_num_targets;
    int turn_count;
} GameState;

Ship ships[SHIP_TYPES] = {
    {4, 1, 'B'},
    {3, 2, 'C'},
    {2, 2, 'D'}};

GtkWidget *window;
GtkWidget *grid_drawing_area1;
GtkWidget *grid_drawing_area2;
GtkWidget *message_label;
GtkWidget *start_button;
GtkWidget *load_button;
GtkWidget *save_button;
GtkWidget *stop_continue_button;
GtkWidget *restart_button;

GameState game_state;
int game_active = 0;
int game_paused = 0;
int pipefd1[2], pipefd2[2];
pid_t child_pid;
guint game_turn_id = 0;

void init_game_state();
void save_game_state();
int load_game_state();
void initialize_grid(char grid[GRID_SIZE][GRID_SIZE]);
int can_place_ship(char grid[GRID_SIZE][GRID_SIZE], int row, int col, int size, int vertical);
void place_ship(char grid[GRID_SIZE][GRID_SIZE], int row, int col, int size, int vertical, char symbol);
void place_ships(char grid[GRID_SIZE][GRID_SIZE]);
int check_hit(char grid[GRID_SIZE][GRID_SIZE], int row, int col);
int all_ships_sunk(char grid[GRID_SIZE][GRID_SIZE]);
void ai_make_guess(int *row, int *col, char ai_knowledge_grid[GRID_SIZE][GRID_SIZE], Position potential_targets[], int *num_potential_targets);
void update_potential_targets(int row, int col, char ai_knowledge_grid[GRID_SIZE][GRID_SIZE], Position potential_targets[], int *num_potential_targets);
gboolean draw_grid(GtkWidget *widget, cairo_t *cr, char grid[GRID_SIZE][GRID_SIZE]);
void start_game();
void end_game();

void init_game_state()
{
    memset(&game_state, 0, sizeof(GameState));
    for (int i = 0; i < GRID_SIZE; i++)
    {
        for (int j = 0; j < GRID_SIZE; j++)
        {
            game_state.grid1[i][j] = '.';
            game_state.grid2[i][j] = '.';
            game_state.ai1_knowledge[i][j] = ' ';
            game_state.ai2_knowledge[i][j] = ' ';
        }
    }
    game_state.ai1_num_targets = 0;
    game_state.ai2_num_targets = 0;
    game_state.turn_count = 0;
}

void save_game_state()
{
    FILE *f = fopen(SAVE_FILE, "wb");
    if (f != NULL)
    {
        fwrite(&game_state, sizeof(GameState), 1, f);
        fclose(f);
        gtk_label_set_text(GTK_LABEL(message_label), "Game saved!");
    }
    else
    {
        gtk_label_set_text(GTK_LABEL(message_label), "Save failed!");
    }
}

int load_game_state()
{
    FILE *f = fopen(SAVE_FILE, "rb");
    if (f != NULL)
    {
        size_t read_size = fread(&game_state, sizeof(GameState), 1, f);
        fclose(f);
        return (read_size == 1);
    }
    return 0;
}

int can_place_ship(char grid[GRID_SIZE][GRID_SIZE], int row, int col, int size, int vertical)
{
    if (vertical)
    {
        if (row + size > GRID_SIZE)
            return 0;
    }
    else
    {
        if (col + size > GRID_SIZE)
            return 0;
    }

    int start_row = row - 1;
    int end_row = row + (vertical ? size : 1);
    int start_col = col - 1;
    int end_col = col + (vertical ? 1 : size);

    for (int r = start_row; r <= end_row; r++)
    {
        for (int c = start_col; c <= end_col; c++)
        {
            if (r >= 0 && r < GRID_SIZE && c >= 0 && c < GRID_SIZE)
            {
                if (grid[r][c] != '.')
                    return 0;
            }
        }
    }
    return 1;
}

void place_ship(char grid[GRID_SIZE][GRID_SIZE], int row, int col, int size, int vertical, char symbol)
{
    for (int i = 0; i < size; i++)
    {
        grid[row + (vertical ? i : 0)][col + (vertical ? 0 : i)] = symbol;
    }
}

void place_ships(char grid[GRID_SIZE][GRID_SIZE])
{
    for (int i = 0; i < SHIP_TYPES; i++)
    {
        for (int j = 0; j < ships[i].count; j++)
        {
            int placed = 0;
            while (!placed)
            {
                int row = rand() % GRID_SIZE;
                int col = rand() % GRID_SIZE;
                int vertical = rand() % 2;
                if (can_place_ship(grid, row, col, ships[i].size, vertical))
                {
                    place_ship(grid, row, col, ships[i].size, vertical, ships[i].symbol);
                    placed = 1;
                }
            }
        }
    }
}

void ai_make_guess(int *row, int *col, char ai_knowledge_grid[GRID_SIZE][GRID_SIZE],
                   Position potential_targets[], int *num_potential_targets)
{
    if (*num_potential_targets > 0)
    {
        int target_index = rand() % *num_potential_targets;
        *row = potential_targets[target_index].row;
        *col = potential_targets[target_index].col;
        potential_targets[target_index] = potential_targets[*num_potential_targets - 1];
        (*num_potential_targets)--;
    }
    else
    {
        int found = 0;
        while (!found)
        {
            *row = rand() % GRID_SIZE;
            *col = rand() % GRID_SIZE;
            if (ai_knowledge_grid[*row][*col] == ' ')
                found = 1;
        }
    }
}

void update_potential_targets(int row, int col, char ai_knowledge_grid[GRID_SIZE][GRID_SIZE],
                              Position potential_targets[], int *num_potential_targets)
{
    int dr[] = {-1, 1, 0, 0};
    int dc[] = {0, 0, -1, 1};

    for (int i = 0; i < 4; i++)
    {
        int nr = row + dr[i];
        int nc = col + dc[i];
        if (nr >= 0 && nr < GRID_SIZE && nc >= 0 && nc < GRID_SIZE)
        {
            if (ai_knowledge_grid[nr][nc] == ' ')
            {
                int already_added = 0;
                for (int j = 0; j < *num_potential_targets; j++)
                {
                    if (potential_targets[j].row == nr && potential_targets[j].col == nc)
                    {
                        already_added = 1;
                        break;
                    }
                }
                if (!already_added && *num_potential_targets < MAX_POTENTIAL_TARGETS)
                {
                    potential_targets[*num_potential_targets].row = nr;
                    potential_targets[*num_potential_targets].col = nc;
                    (*num_potential_targets)++;
                }
            }
        }
    }
}

int check_hit(char grid[GRID_SIZE][GRID_SIZE], int row, int col)
{
    if (grid[row][col] == 'B' || grid[row][col] == 'C' || grid[row][col] == 'D')
    {
        grid[row][col] = 'X';
        return 1;
    }
    grid[row][col] = 'M';
    return 0;
}

int all_ships_sunk(char grid[GRID_SIZE][GRID_SIZE])
{
    for (int i = 0; i < GRID_SIZE; i++)
    {
        for (int j = 0; j < GRID_SIZE; j++)
        {
            if (grid[i][j] == 'B' || grid[i][j] == 'C' || grid[i][j] == 'D')
                return 0;
        }
    }
    return 1;
}

gboolean draw_grid(GtkWidget *widget, cairo_t *cr, char grid[GRID_SIZE][GRID_SIZE])
{
    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);
    int cell_size = MIN(width, height) / GRID_SIZE;

    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgb(cr, 0, 0, 0);

    for (int i = 0; i <= GRID_SIZE; i++)
    {
        cairo_move_to(cr, i * cell_size, 0);
        cairo_line_to(cr, i * cell_size, GRID_SIZE * cell_size);
        cairo_move_to(cr, 0, i * cell_size);
        cairo_line_to(cr, GRID_SIZE * cell_size, i * cell_size);
    }
    cairo_stroke(cr);

    for (int i = 0; i < GRID_SIZE; i++)
    {
        for (int j = 0; j < GRID_SIZE; j++)
        {
            char cell = grid[i][j];
            if (cell != '.')
            {
                cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
                cairo_set_font_size(cr, cell_size * 0.8);

                cairo_text_extents_t extents;
                char text[2] = {cell, '\0'};
                cairo_text_extents(cr, text, &extents);

                double x = j * cell_size + (cell_size - extents.width) / 2;
                double y = (i + 1) * cell_size - (cell_size - extents.height) / 2;

                if (cell == 'X')
                    cairo_set_source_rgb(cr, 1, 0, 0);
                else if (cell == 'M')
                    cairo_set_source_rgb(cr, 0, 0, 1);
                else
                    cairo_set_source_rgb(cr, 0, 0, 0);

                cairo_move_to(cr, x, y);
                cairo_show_text(cr, text);
            }
        }
    }
    return TRUE;
}

gboolean on_draw_grid1(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    gboolean result = draw_grid(widget, cr, game_state.grid1);
    cairo_stroke(cr); // Ensure all drawing operations are completed
    return result;
}

gboolean on_draw_grid2(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    gboolean result = draw_grid(widget, cr, game_state.grid2);
    cairo_stroke(cr); // Ensure all drawing operations are completed
    return result;
}

void update_display()
{
    // Force immediate redraw of both grids
    gtk_widget_queue_draw_area(grid_drawing_area1, 0, 0,
                               gtk_widget_get_allocated_width(grid_drawing_area1),
                               gtk_widget_get_allocated_height(grid_drawing_area1));

    gtk_widget_queue_draw_area(grid_drawing_area2, 0, 0,
                               gtk_widget_get_allocated_width(grid_drawing_area2),
                               gtk_widget_get_allocated_height(grid_drawing_area2));

    // Process pending events to ensure immediate update
    while (gtk_events_pending())
        gtk_main_iteration();
}

gboolean game_turn(gpointer data)
{
    if (!game_active || game_paused)
        return TRUE;

    game_state.turn_count++;

    int row, col;
    ai_make_guess(&row, &col, game_state.ai1_knowledge, game_state.ai1_targets, &game_state.ai1_num_targets);

    write(pipefd1[1], &row, sizeof(int));
    write(pipefd1[1], &col, sizeof(int));

    int result;
    read(pipefd2[0], &result, sizeof(int));

    if (result == -1)
    {
        gtk_label_set_text(GTK_LABEL(message_label), "AI 1 wins!");
        end_game();
        return FALSE;
    }

    if (result)
    {
        game_state.ai1_knowledge[row][col] = 'X';
        game_state.grid2[row][col] = 'X';
        update_potential_targets(row, col, game_state.ai1_knowledge, game_state.ai1_targets, &game_state.ai1_num_targets);
    }
    else
    {
        game_state.ai1_knowledge[row][col] = 'M';
        game_state.grid2[row][col] = 'M';
    }

    read(pipefd2[0], &row, sizeof(int));
    read(pipefd2[0], &col, sizeof(int));

    result = check_hit(game_state.grid1, row, col);

    if (all_ships_sunk(game_state.grid1))
    {
        result = -1;
        write(pipefd1[1], &result, sizeof(int));
        gtk_label_set_text(GTK_LABEL(message_label), "AI 2 wins!");
        end_game();
        return FALSE;
    }
    else
    {
        write(pipefd1[1], &result, sizeof(int));
        game_state.grid1[row][col] = result ? 'X' : 'M';
    }

    if (result)
    {
        game_state.ai2_knowledge[row][col] = 'X';
        update_potential_targets(row, col, game_state.ai2_knowledge, game_state.ai2_targets, &game_state.ai2_num_targets);
    }
    else
    {
        game_state.ai2_knowledge[row][col] = 'M';
    }

    update_display();
    return TRUE;
}

void end_game()
{
    if (!game_active)
        return;
    game_active = 0;
    game_paused = 0;
    close(pipefd1[1]);
    close(pipefd2[0]);
    kill(child_pid, SIGTERM);
    waitpid(child_pid, NULL, 0);
    if (game_turn_id)
    {
        g_source_remove(game_turn_id);
        game_turn_id = 0;
    }
    gtk_button_set_label(GTK_BUTTON(stop_continue_button), "Stop");
}

void start_game()
{
    if (game_active)
        return;

    init_game_state();
    place_ships(game_state.grid1);
    place_ships(game_state.grid2);

    pipe(pipefd1);
    pipe(pipefd2);

    child_pid = fork();

    if (child_pid == 0)
    {
        close(pipefd1[1]);
        close(pipefd2[0]);

        while (1)
        {
            int row, col, result;
            read(pipefd1[0], &row, sizeof(int));
            read(pipefd1[0], &col, sizeof(int));

            result = check_hit(game_state.grid2, row, col);

            if (all_ships_sunk(game_state.grid2))
            {
                result = -1;
                write(pipefd2[1], &result, sizeof(int));
                exit(0);
            }

            write(pipefd2[1], &result, sizeof(int));

            ai_make_guess(&row, &col, game_state.ai2_knowledge, game_state.ai2_targets, &game_state.ai2_num_targets);

            write(pipefd2[1], &row, sizeof(int));
            write(pipefd2[1], &col, sizeof(int));

            read(pipefd1[0], &result, sizeof(int));

            if (result == -1)
                exit(0);

            if (result)
            {
                game_state.ai2_knowledge[row][col] = 'X';
                update_potential_targets(row, col, game_state.ai2_knowledge, game_state.ai2_targets, &game_state.ai2_num_targets);
            }
            else
            {
                game_state.ai2_knowledge[row][col] = 'M';
            }
        }
    }

    game_active = 1;
    game_paused = 0;
    game_turn_id = g_timeout_add(1000, game_turn, NULL);
    gtk_button_set_label(GTK_BUTTON(stop_continue_button), "Stop");
}

void on_start_clicked(GtkWidget *widget, gpointer data)
{
    start_game();
}

void on_save_clicked(GtkWidget *widget, gpointer data)
{
    if (game_active)
        save_game_state();
    else
        gtk_label_set_text(GTK_LABEL(message_label), "No active game!");
}

void on_load_clicked(GtkWidget *widget, gpointer data)
{
    if (load_game_state())
        start_game();
    else
        gtk_label_set_text(GTK_LABEL(message_label), "No saved game found.");
}

void on_stop_continue_clicked(GtkWidget *widget, gpointer data)
{
    if (!game_active)
        return;

    game_paused = !game_paused;
    gtk_button_set_label(GTK_BUTTON(stop_continue_button), game_paused ? "Continue" : "Stop");
    gtk_label_set_text(GTK_LABEL(message_label), game_paused ? "Game paused" : "Game resumed");
}

void on_restart_clicked(GtkWidget *widget, gpointer data)
{
    if (game_active)
    {
        end_game();
    }
    start_game();
    gtk_label_set_text(GTK_LABEL(message_label), "Game restarted!");
}

void create_gui()
{
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Battleship");
    gtk_container_set_border_width(GTK_CONTAINER(window), 10);
    g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);

    GtkWidget *frame1 = gtk_frame_new("AI 1's Grid");
    gtk_box_pack_start(GTK_BOX(hbox), frame1, TRUE, TRUE, 0);

    grid_drawing_area1 = gtk_drawing_area_new();
    gtk_widget_set_size_request(grid_drawing_area1, 300, 300);
    g_signal_connect(G_OBJECT(grid_drawing_area1), "draw", G_CALLBACK(on_draw_grid1), NULL);
    g_signal_connect(G_OBJECT(grid_drawing_area1), "size-allocate", G_CALLBACK(gtk_widget_queue_draw), NULL);
    gtk_container_add(GTK_CONTAINER(frame1), grid_drawing_area1);

    GtkWidget *frame2 = gtk_frame_new("AI 2's Grid");
    gtk_box_pack_start(GTK_BOX(hbox), frame2, TRUE, TRUE, 0);

    grid_drawing_area2 = gtk_drawing_area_new();
    gtk_widget_set_size_request(grid_drawing_area2, 300, 300);
    g_signal_connect(G_OBJECT(grid_drawing_area2), "draw", G_CALLBACK(on_draw_grid2), NULL);
    g_signal_connect(G_OBJECT(grid_drawing_area2), "size-allocate", G_CALLBACK(gtk_widget_queue_draw), NULL);
    gtk_container_add(GTK_CONTAINER(frame2), grid_drawing_area2);

    message_label = gtk_label_new("Welcome to Battleship!");
    gtk_box_pack_start(GTK_BOX(vbox), message_label, FALSE, FALSE, 5);

    GtkWidget *button_box = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(button_box), GTK_BUTTONBOX_CENTER);
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 0);

    start_button = gtk_button_new_with_label("Start New Game");
    g_signal_connect(G_OBJECT(start_button), "clicked", G_CALLBACK(on_start_clicked), NULL);
    gtk_container_add(GTK_CONTAINER(button_box), start_button);

    stop_continue_button = gtk_button_new_with_label("Stop");
    g_signal_connect(G_OBJECT(stop_continue_button), "clicked", G_CALLBACK(on_stop_continue_clicked), NULL);
    gtk_container_add(GTK_CONTAINER(button_box), stop_continue_button);

    restart_button = gtk_button_new_with_label("Refresh Grid / Restart Game");
    g_signal_connect(G_OBJECT(restart_button), "clicked", G_CALLBACK(on_restart_clicked), NULL);
    gtk_container_add(GTK_CONTAINER(button_box), restart_button);

    save_button = gtk_button_new_with_label("Save Game");
    g_signal_connect(G_OBJECT(save_button), "clicked", G_CALLBACK(on_save_clicked), NULL);
    gtk_container_add(GTK_CONTAINER(button_box), save_button);

    load_button = gtk_button_new_with_label("Load Game");
    g_signal_connect(G_OBJECT(load_button), "clicked", G_CALLBACK(on_load_clicked), NULL);
    gtk_container_add(GTK_CONTAINER(button_box), load_button);

    gtk_widget_show_all(window);
}

void cleanup()
{
    if (game_active)
        end_game();
}

int main(int argc, char *argv[])
{
    srand(time(NULL));
    gtk_init(&argc, &argv);
    create_gui();
    g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(cleanup), NULL);
    gtk_main();
    return 0;
}