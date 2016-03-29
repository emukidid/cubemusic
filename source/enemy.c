#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gccore.h>
#include <errno.h>
#include <math.h>
#include <unistd.h>
#include <malloc.h>
#include <stdarg.h>
#include "enemy.h"

static Enemy **enemies;
int num_enemies;

void enemy_init(int num) {
	if(enemies) {
		free(enemies);
	}
	num_enemies = num;
	enemies = malloc(sizeof(unsigned int) * num);

	int i = 0;
	for(i = 0; i < num; i++) {
		enemies[i] = malloc(sizeof(Enemy));
		memset(enemies[i], 0, sizeof(Enemy));
	}
	printf("Initialised %i enemies totalling %i bytes\r\n", num, 
		(sizeof(Enemy)*num) + (sizeof(unsigned int)*num));
}

void enemy_setdamage(Enemy *e, int damage) {
	// Inflict damage on an enemy
	e->health -= damage;
}

int enemy_gethealth(Enemy *e) {
	return e->health;
}

void enemy_update() {
	// Update some stuff each frame.
	
}

void enemy_deinit() {
	if(enemies) {
		free(enemies);
	}
	num_enemies = 0;
}