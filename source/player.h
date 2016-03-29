#ifndef PLAYER_H_
#define PLAYER_H_

typedef struct {
	int x;
	int y;
	int direction;
	int speed;
	int damage_power;
} Projectile;

#define MAX_PROJECTILES 5

typedef struct {
	int x;
	int y;
	int x_min;
	int x_max;
	int y_min;
	int y_max;
	int health;
	int direction;
	int speed;
	Projectile projectiles[MAX_PROJECTILES];
} Player;

void player_init(int health, int x_min, int x_max, int x, int y_min, int y_max, int y);
void player_adj_health(int amt);
void player_deinit();
void player_update();
#endif
