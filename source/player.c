#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gccore.h>
#include "player.h"

static Player player;

void player_init(int health, int x_min, int x_max, int x, int y_min, int y_max, int y) {
	memset(&player, 0, sizeof(Player));
	player.health = health;
	player.x_min = x_min;
	player.x_max = x_max;
	player.x = x;
	player.y_min = y_min;
	player.y_max = y_max;
	player.y = y;
}

void player_adj_health(int amt) {
	player.health += amt;
}

void player_deinit() {

}

void player_update() {
	
}
