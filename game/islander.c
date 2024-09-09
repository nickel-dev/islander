const int tile_width = 6;
int world_pos_to_tile_pos(float world_pos) {
	return roundf(world_pos / (float)tile_width);
}

float tile_pos_to_world_pos(int tile_pos) {
	return ((float)tile_pos * (float)tile_width);
}

Vector2 round_v2_to_tile(Vector2 world_pos) {
	world_pos.x = tile_pos_to_world_pos(world_pos_to_tile_pos(world_pos.x));
	world_pos.y = tile_pos_to_world_pos(world_pos_to_tile_pos(world_pos.y));
	return world_pos;
}

bool almost_equals(float a, float b, float epsilon) {
 return fabs(a - b) <= epsilon;
}

bool animate_f32_to_target(float* value, float target, float delta_t, float rate) {
	*value += (target - *value) * (1.0 - pow(2.0f, -rate * delta_t));
	if (almost_equals(*value, target, 0.001f)) {
		*value = target;
		return true; // reached
	}
	return false;
}

void animate_v2_to_target(Vector2* value, Vector2 target, float delta_t, float rate) {
	animate_f32_to_target(&(value->x), target.x, delta_t, rate);
	animate_f32_to_target(&(value->y), target.y, delta_t, rate);
}

Vector2 screen_to_world() {
	float mouse_x = input_frame.mouse_x;
	float mouse_y = input_frame.mouse_y;
	Matrix4 proj = draw_frame.projection;
	Matrix4 view = draw_frame.camera_xform;
	float window_w = window.width;
	float window_h = window.height;

	// Normalize the mouse coordinates
	float ndc_x = (mouse_x / (window_w * 0.5f)) - 1.0f;
	float ndc_y = (mouse_y / (window_h * 0.5f)) - 1.0f;

	// Transform to world coordinates
	Vector4 world_pos = v4(ndc_x, ndc_y, 0, 1);
	world_pos = m4_transform(m4_inverse(proj), world_pos);
	world_pos = m4_transform(view, world_pos);
	// log("%f, %f", world_pos.x, world_pos.y);

	// Return as 2D vector
	return (Vector2){ world_pos.x, world_pos.y };
}

// Entities
typedef enum Entity_Archetype {
	ARCH_nil = 0, ARCH_rock, ARCH_tree, ARCH_player, ARCH_raft_tile
} Entity_Archetype;

typedef struct Entity {
	bool is_valid;
	Vector2 pos;

	u32 arch;
	u32 sprite_id;
} Entity;

// Sprites
typedef enum Sprite_ID {
	SPRITE_nil = 0, SPRITE_player, SPRITE_rock0, SPRITE_rock1, SPRITE_select, SPRITE_raft_tile, SPRITE_inventory, SPRITE_MAX
} Sprite_ID;

typedef struct Sprite {
	Gfx_Image* image;
	Vector2 size;
} Sprite;
Sprite sprites[SPRITE_MAX];

Sprite* get_sprite(u32 id) {
	if (id < SPRITE_MAX)
		return &sprites[id];
	return &sprites[0];
}

// State
#define MAX_ENTITY_COUNT 512
typedef struct State {
	Entity entities[MAX_ENTITY_COUNT];
} State;
State* state = 0;

// We don't use and entity_count, because there is the is_valid flag. This makes dead Entities reusable.
Entity* new_entity() {
	Entity* result = 0;
	for (int i = 0; i < MAX_ENTITY_COUNT; ++i) {
		result = &state->entities[i];
		if (!result->is_valid)
			break;
	}
	assert(result, "Entity count exceeded the limit of %i.", MAX_ENTITY_COUNT);
	result->is_valid = true;
	return result;
}

void delete_entity(Entity* e) {
	// Will also automatically set is_valid to false
	memset(e, 0, sizeof(Entity));
}

void load_sprite(u32 sprite_id, string path) {
	Sprite s;
	s.image = load_image_from_disk(path, get_heap_allocator());
	assert(s.image, "Loading sprite id %i failed!", sprite_id);
	s.size = v2(s.image->width, s.image->height);
	sprites[sprite_id] = s;
}

Entity* setup_player() {
	Entity* e = new_entity();
	e->arch = ARCH_player;
	e->sprite_id = SPRITE_player;
	return e;
}

Entity* setup_rock() {
	Entity* e = new_entity();
	e->arch = ARCH_rock;
	// Picks random rock
	e->sprite_id = get_random_int_in_range(SPRITE_rock0, SPRITE_rock1);
	return e;
}

Entity* setup_raft_tile() {
	Entity* e = new_entity();
	e->arch = ARCH_raft_tile;
	e->sprite_id = SPRITE_raft_tile;
	return e;
}

int entry(int argc, char **argv) {
	window.title = STR("Islander");
	window.width = 1280;
	window.height = 720;
	window.x = 200;
	window.y = 200;
	window.force_topmost = false;
	window.clear_color = v4(0.4, 0.7, 1, 1); // :water

	Gfx_Font* debug_font = load_font_from_disk(STR("C:/Windows/Fonts/Arial.ttf"), get_heap_allocator());
	assert(debug_font, "Failed to load the debug font!");
	u32 font_size = 48;

	load_sprite(SPRITE_nil, STR("sprites/white.png"));
	load_sprite(SPRITE_player, STR("sprites/player.png"));
	load_sprite(SPRITE_rock0, STR("sprites/rock0.png"));
	load_sprite(SPRITE_rock1, STR("sprites/rock1.png"));
	load_sprite(SPRITE_select, STR("sprites/selection.png"));
	load_sprite(SPRITE_raft_tile, STR("sprites/raft_tile.png"));
	load_sprite(SPRITE_inventory, STR("sprites/inventory.png"));

	state = alloc(get_heap_allocator(), sizeof(State));

	Entity* player_entity = setup_player();

	for (int y = 0; y < 4; ++y) {
		for (int x = 0; x < 4; ++x) {
			Entity* e = setup_raft_tile();
			e->pos = v2(x * 30, y * 30);
		}
	}

	for (int i = 0; i < 10; ++i) {
		Entity* e = setup_rock();
		e->pos = v2(get_random_float32_in_range(-200, 200), get_random_float32_in_range(-200, 200));
		e->pos = round_v2_to_tile(e->pos);
	}

	f32 zoom = 5.3;
	Vector2 camera_pos = v2(0, 0);

	f32 seconds_counter = 0.0;
	u64 frame_count = 0;
	f64 last_time = os_get_elapsed_seconds();
	while (!window.should_close) {
		reset_temporary_storage();

		f64 now = os_get_elapsed_seconds();
		f64 delta_t = now - last_time;
		last_time = now;

		draw_frame.projection = m4_make_orthographic_projection(-window.width / 2, window.width / 2, -window.height / 2, window.height / 2, -1, 10);

		// :camera
		{
			zoom += input_frame.events->yscroll * 100 * delta_t;
			input_frame.events->yscroll = 0;

			animate_v2_to_target(&camera_pos, player_entity->pos, delta_t, 10);

			draw_frame.camera_xform = m4_make_scale(v3_scalar(1));
			draw_frame.camera_xform = m4_mul(draw_frame.camera_xform, m4_make_translation(v3(camera_pos.x, camera_pos.y, 1)));
			draw_frame.camera_xform = m4_mul(draw_frame.camera_xform, m4_make_scale(v3(1.0 / zoom, 1.0 / zoom, 1)));
		}
		
		if (is_key_just_pressed(KEY_ESCAPE))
			window.should_close = true;
		
		if (is_key_just_pressed(KEY_F11))
			window.fullscreen = !window.fullscreen;

		os_update();

		// Player movement
		{
			Vector2 input_axis = v2(0, 0);
			if (is_key_down('A'))
				input_axis.x -= 1.0;
			if (is_key_down('D'))
				input_axis.x += 1.0;
			if (is_key_down('S'))
				input_axis.y -= 1.0;
			if (is_key_down('W'))
				input_axis.y += 1.0;
			input_axis = v2_normalize(input_axis);

			player_entity->pos = v2_add(player_entity->pos, v2_mulf(input_axis, 50.0 * delta_t));
		}

		Vector2 mouse_pos = screen_to_world();
		int mouse_tile_x = world_pos_to_tile_pos(mouse_pos.x);
		int mouse_tile_y = world_pos_to_tile_pos(mouse_pos.y);
		Vector2 mouse_tile_pos = v2(mouse_tile_x * tile_width, mouse_tile_y * tile_width);

		// :render_tiles
		{
			int player_tile_x = world_pos_to_tile_pos(player_entity->pos.x);
			int player_tile_y = world_pos_to_tile_pos(player_entity->pos.y);
			int tile_radius_x = 40;
			int tile_radius_y = 30;
			for (int x = player_tile_x - tile_radius_x; x < player_tile_x + tile_radius_x; x++) {
				for (int y = player_tile_y - tile_radius_y; y < player_tile_y + tile_radius_y; y++) {
					if ((x + (y % 2 == 0) ) % 2 == 0) {
						Vector4 col = v4(0, 0, 0, 0.05);
						float x_pos = x * tile_width;
						float y_pos = y * tile_width;
						draw_rect(v2(x_pos + tile_width * -0.5, y_pos + tile_width * -0.5), v2(tile_width, tile_width), col);
					}
				}
			}
			//draw_rect(v2(tile_pos_to_world_pos(mouse_tile_x) + tile_width * -0.5, tile_pos_to_world_pos(mouse_tile_y) + tile_width * -0.5), v2(tile_width, tile_width), v4(0.5, 0.5, 0.5, 0.5));
		}

		// :render
		for (int i = 1; i < MAX_ENTITY_COUNT; ++i) {
			Entity* e = &state->entities[i];
			if (!e->is_valid)
				continue;

			f32 yoffset = -tile_width / 2;
			Sprite* sprite = get_sprite(e->sprite_id);
			Matrix4 xform = m4_scalar(1.0);
			xform = m4_translate(xform, v3(e->pos.x, e->pos.y, 0));
			xform = m4_translate(xform, v3(-sprite->size.x / 2, yoffset, 0));
			draw_image_xform(sprite->image, xform, sprite->size, COLOR_WHITE);
			
			Range2f bounds = range2f_make_bottom_center(sprite->size);
			bounds = range2f_shift(bounds, e->pos);
			if (range2f_contains(bounds, mouse_tile_pos))
				draw_image_xform(sprite->image, xform, sprite->size, v4(2, 2, 2, .35));
		}

		// :render_player
		{
			Entity* e = player_entity;
			Sprite* sprite = get_sprite(e->sprite_id);
			Matrix4 xform = m4_scalar(1.0);
			xform = m4_translate(xform, v3(e->pos.x, e->pos.y, 0));
			xform = m4_translate(xform, v3(-sprite->size.x / 2, 0, 0));
			draw_image_xform(sprite->image, xform, sprite->size, COLOR_WHITE);
		}

		// :select
		{
			Sprite* sprite = get_sprite(SPRITE_select);
			Matrix4 xform = m4_scalar(1.0);
			xform = m4_translate(xform, v3(mouse_tile_pos.x, mouse_tile_pos.y, 0));
			xform = m4_translate(xform, v3(-sprite->size.x / 2, -sprite->size.y / 2, 0));
			draw_image_xform(sprite->image, xform, sprite->size, COLOR_WHITE);
		}

		// :inventory
		/*{
			Sprite* sprite = get_sprite(SPRITE_inventory);
			Matrix4 xform = m4_scalar(1.0);
			xform = m4_translate(xform, v3(-128, 0, 0));
			xform = m4_mul(xform, draw_frame.camera_xform);
			xform = m4_mul(xform, m4_inverse(draw_frame.projection));
			draw_image_xform(sprite->image, xform, v2(sprite->size.x / window.width * tile_width * 2, sprite->size.y / window.height * tile_width * 2), COLOR_WHITE);
		}*/

		gfx_update();
		seconds_counter += delta_t;
		frame_count += 1;
		if (seconds_counter >= 1.0) {
			log("fps: %i", frame_count);
			seconds_counter = 0.0;
			frame_count = 0;
		}
	}

	return 0;
}