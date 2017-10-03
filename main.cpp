#include "load_save_png.hpp"
#include "GL.hpp"
#include "Meshes.hpp"
#include "Scene.hpp"
#include "read_chunk.hpp"

#include <SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <fstream>

#include <unistd.h>

static GLuint compile_shader(GLenum type, std::string const &source);
static GLuint link_program(GLuint vertex_shader, GLuint fragment_shader);

static const std::string D1 = std::string("Damp1");
static const std::string D2 = std::string("Damp2");
static const std::string Field = std::string("Field");
static const std::string LF = std::string("LowFric");
static const std::string Ball = std::string("Ball");
static const std::string P1 = std::string("Paddle1");
static const std::string P2 = std::string("Paddle2");
static const std::string W1 = std::string("Wall1");
static const std::string W2 = std::string("Wall2");

bool exclusion(Scene::Object *obj1, Scene::Object *obj2);
bool s2s_collision(Scene::Object *obj1,Scene::Object *obj2);
bool b2s_collision(Scene::Object *block,Scene::Object *sphere);
bool inclusion(Scene::Object *obj1, Scene::Object *obj2);
bool s2p_collision(Scene::Object *obj, Scene::Object *temp, float theta, bool dir, glm::vec2 &normal);
void knock(glm::vec2 &ball_velocity, glm::vec2 normal);
void reflect(glm::vec2 &ball_velocity, glm::vec2 normal);
void reflect(glm::vec2 &ball_velocity, glm::vec2 normal, float damp);

int main(int argc, char **argv) {
	//Configuration:
	struct {
		std::string title = "Game2: Scene";
		glm::uvec2 size = glm::uvec2(640, 480);
	} config;

	//------------  initialization ------------

	//Initialize SDL library:
	SDL_Init(SDL_INIT_VIDEO);

	//Ask for an OpenGL context version 3.3, core profile, enable debug:
	SDL_GL_ResetAttributes();
	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

	//create window:
	SDL_Window *window = SDL_CreateWindow(
		config.title.c_str(),
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		config.size.x, config.size.y,
		SDL_WINDOW_OPENGL /*| SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI*/
	);

	if (!window) {
		std::cerr << "Error creating SDL window: " << SDL_GetError() << std::endl;
		return 1;
	}

	//Create OpenGL context:
	SDL_GLContext context = SDL_GL_CreateContext(window);

	if (!context) {
		SDL_DestroyWindow(window);
		std::cerr << "Error creating OpenGL context: " << SDL_GetError() << std::endl;
		return 1;
	}

	#ifdef _WIN32
	//On windows, load OpenGL extensions:
	if (!init_gl_shims()) {
		std::cerr << "ERROR: failed to initialize shims." << std::endl;
		return 1;
	}
	#endif

	//Set VSYNC + Late Swap (prevents crazy FPS):
	if (SDL_GL_SetSwapInterval(-1) != 0) {
		std::cerr << "NOTE: couldn't set vsync + late swap tearing (" << SDL_GetError() << ")." << std::endl;
		if (SDL_GL_SetSwapInterval(1) != 0) {
			std::cerr << "NOTE: couldn't set vsync (" << SDL_GetError() << ")." << std::endl;
		}
	}

	//Hide mouse cursor (note: showing can be useful for debugging):
	//SDL_ShowCursor(SDL_DISABLE);

	//------------ opengl objects / game assets ------------

	//shader program:
	GLuint program = 0;
	GLuint program_Position = 0;
	GLuint program_Normal = 0;
	GLuint program_mvp = 0;
	GLuint program_itmv = 0;
	GLuint program_to_light = 0;
	GLuint program_Color = 0;
	{ //compile shader program:
		GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER,
			"#version 330\n"
			"uniform mat4 mvp;\n"
			"uniform mat3 itmv;\n"
			"in vec4 Position;\n"
			"in vec3 Normal;\n"
			"in vec3 Color;\n"
			"out vec3 normal;\n"
			"out vec3 color;\n"
			"void main() {\n"
			"	gl_Position = mvp * Position;\n"
			"	normal = itmv * Normal;\n"
			"	color = Color;\n"
			"}\n"
		);

		GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER,
			"#version 330\n"
			"uniform vec3 to_light;\n"
			"in vec3 normal;\n"
			"in vec3 color;\n"
			"out vec4 fragColor;\n"
			"void main() {\n"
			"	float light = max(0.0, dot(normalize(normal), to_light));\n"
			"	fragColor = vec4(light * color, 1.0);\n"
			"}\n"
		);

		program = link_program(fragment_shader, vertex_shader);

		//look up attribute locations:
		program_Position = glGetAttribLocation(program, "Position");
		if (program_Position == -1U) throw std::runtime_error("no attribute named Position");
		program_Normal = glGetAttribLocation(program, "Normal");
		if (program_Normal == -1U) throw std::runtime_error("no attribute named Normal");
		program_Color = glGetAttribLocation(program, "Color");

		//look up uniform locations:
		program_mvp = glGetUniformLocation(program, "mvp");
		if (program_mvp == -1U) throw std::runtime_error("no uniform named mvp");
		program_itmv = glGetUniformLocation(program, "itmv");
		if (program_itmv == -1U) throw std::runtime_error("no uniform named itmv");

		program_to_light = glGetUniformLocation(program, "to_light");
		if (program_to_light == -1U) throw std::runtime_error("no uniform named to_light");
	}

	//--------- Game constants -------

	std::map <std::string, Scene::Object*> n2o;

	//------------ meshes ------------

	Meshes meshes;

	{ //add meshes to database:
		Meshes::Attributes attributes;
		attributes.Position = program_Position;
		attributes.Normal = program_Normal;
		attributes.Color = program_Color;

		meshes.load("meshes.blob", attributes);
	}
	
	//------------ scene ------------

	Scene scene;
	//set up camera parameters based on window:
	scene.camera.fovy = glm::radians(60.0f);
	scene.camera.aspect = float(config.size.x) / float(config.size.y);
	scene.camera.near = 0.01f;
	//(transform will be handled in the update function below)

	//add some objects from the mesh library:
	auto add_object = [&](std::string const &name, glm::vec3 const &position, glm::quat const &rotation, glm::vec3 const &scale, glm::vec3 const &dimension) -> Scene::Object & {
		Mesh const &mesh = meshes.get(name);
		scene.objects.emplace_back();
		Scene::Object &object = scene.objects.back();
		object.transform.position = position;
		object.transform.rotation = rotation;
		object.transform.scale = scale;
		object.vao = mesh.vao;
		object.start = mesh.start;
		object.count = mesh.count;
		object.program = program;
		object.program_mvp = program_mvp;
		object.program_itmv = program_itmv;
		object.dimension = dimension;
		if (name == "Paddle1" || name == "Paddle2") {
			object.radius = glm::vec2(dimension.x, dimension.x);
		} else {
			object.radius = glm::vec2(dimension.x / 2.0f, dimension.y / 2.0f);
		}
		n2o[name] = &object;
		return object;
	};


	{ //read objects to add from "scene.blob":
		std::ifstream file("scene.blob", std::ios::binary);

		std::vector< char > strings;
		//read strings chunk:
		read_chunk(file, "str0", &strings);

		{ //read scene chunk, add meshes to scene:
			struct SceneEntry {
				uint32_t name_begin, name_end;
				glm::vec3 position;
				glm::quat rotation;
				glm::vec3 scale;
				glm::vec3 dimension;
			};
			static_assert(sizeof(SceneEntry) == 60, "Scene entry should be packed");

			std::vector< SceneEntry > data;
			read_chunk(file, "scn0", &data);

			for (auto const &entry : data) {
				if (!(entry.name_begin <= entry.name_end && entry.name_end <= strings.size())) {
					throw std::runtime_error("index entry has out-of-range name begin/end");
				}
				std::string name(&strings[0] + entry.name_begin, &strings[0] + entry.name_end);
				add_object(name, entry.position, entry.rotation, entry.scale, entry.dimension);
			}
		}
	}

	glm::vec2 mouse = glm::vec2(0.0f, 0.0f); //mouse position in [-1,1]x[-1,1] coordinates

	struct {
		float radius = 10.0f;
		float elevation = 1.0f;
		float azimuth = 0.0f;
		glm::vec3 target = glm::vec3(0.0f, 0.0f, 0.0f);
	} camera;

	//------------ game loop ------------

	// default direction of P1
	const bool CCW = false;
	const bool CW = true;
	bool p1_dir = CCW;
	bool p2_dir = CW;

	Scene::Object *obj;

	// paddle movement and rotation variables
	obj = n2o.find(P1)->second;
	float p1_angle = glm::angle(obj->transform.rotation);

	obj = n2o.find(P2)->second;
	float p2_angle = glm::angle(obj->transform.rotation);

	const float rotation_speed  = 5.0f;
	const float paddle_speed = 2.0f;

	bool player1_win = false;
	bool player2_win = false;
	bool game_over = false;
	//-----------------------------------

	// velocity of the ball
	glm::vec2 ball_velocity = glm::vec2(0.0f, 0.0f);

	bool should_quit = false;
	while (!game_over) {
		static SDL_Event evt;
		while (SDL_PollEvent(&evt) == 1) {
			//handle input:
			if (evt.type == SDL_MOUSEMOTION) {
				glm::vec2 old_mouse = mouse;
				mouse.x = (evt.motion.x + 0.5f) / float(config.size.x) * 2.0f - 1.0f;
				mouse.y = (evt.motion.y + 0.5f) / float(config.size.y) *-2.0f + 1.0f;
				if (evt.motion.state & SDL_BUTTON(SDL_BUTTON_LEFT)) {
					camera.elevation += -2.0f * (mouse.y - old_mouse.y);
					camera.azimuth += -2.0f * (mouse.x - old_mouse.x);
				}
			} else if (evt.type == SDL_MOUSEBUTTONDOWN) {
			} else if (evt.type == SDL_KEYDOWN && evt.key.repeat == 0) {
				switch(evt.key.keysym.sym) {
					case SDLK_ESCAPE:
						should_quit = true;
						break;
					case SDLK_q:
						if (p2_dir == CW) {
							p2_dir = CCW;
						} else {
							p2_dir = CW;
						}
						break;
					case SDLK_SLASH:
						if (p1_dir == CW) {
							p1_dir = CCW;
						} else {
							p1_dir = CW;
						}
						break;
					default:
						break;
				}
			} else if (evt.type == SDL_QUIT) {
				should_quit = true;
				break;
			}
		}
		if (should_quit) break;

		auto current_time = std::chrono::high_resolution_clock::now();
		static auto previous_time = current_time;
		float elapsed = std::chrono::duration< float >(current_time - previous_time).count();
		previous_time = current_time;

		{ //update game state:
			//tree stack:
			/*
			for (uint32_t i = 0; i < tree_stack.size(); ++i) {
				wave_acc[i] += elapsed * (0.3f + 0.3f * i);
				wave_acc[i] -= std::floor(wave_acc[i]);
				float ang = (0.7f * float(M_PI)) * i;
				tree_stack[i]->transform.rotation = glm::angleAxis(
					std::cos(wave_acc[i] * 2.0f * float(M_PI)) * (0.2f + 0.1f * i),
					glm::vec3(std::cos(ang), std::sin(ang), 0.0f)
				);
			}
			*/

			// damp ball velocity
			ball_velocity = 0.995f * ball_velocity;


			// Do state updates for paddle 1
			obj = n2o.find(P1)->second;
			// update the angle of rotation
			if (p1_dir == CW) {
				p1_angle -= elapsed * rotation_speed;
				obj->transform.rotation = glm::angleAxis(p1_angle,
					glm::vec3(0.0f, 0.0f, 1.0f));
			} else {
				p1_angle += elapsed * rotation_speed;
				obj->transform.rotation = glm::angleAxis(p1_angle,
					glm::vec3(0.0f, 0.0f, 1.0f));
			}
			// update the position
			// Check keystates
			auto *keystate = SDL_GetKeyboardState(NULL);
			if (keystate[SDL_SCANCODE_UP]) {
				obj->transform.position.x -= elapsed * paddle_speed;
				// Check collisions
				bool collision = false;
				collision |= s2s_collision(obj, n2o.find(P2)->second);
				collision |= s2s_collision(obj, n2o.find(D1)->second);
				collision |= s2s_collision(obj, n2o.find(D2)->second);
				collision |= b2s_collision(n2o.find(W1)->second, obj);
				collision |= b2s_collision(n2o.find(W2)->second, obj);
				collision |= exclusion(obj, n2o.find(Field)->second);
				if (collision) {
					obj->transform.position.x += elapsed * paddle_speed;
				}
			}
			if (keystate[SDL_SCANCODE_RIGHT]) {
				obj->transform.position.y += elapsed * paddle_speed;
				// Check collisions
				bool collision = false;
				collision |= s2s_collision(obj, n2o.find(P2)->second);
				collision |= s2s_collision(obj, n2o.find(D1)->second);
				collision |= s2s_collision(obj, n2o.find(D2)->second);
				collision |= b2s_collision(n2o.find(W1)->second, obj);
				collision |= b2s_collision(n2o.find(W2)->second, obj);
				collision |= exclusion(obj, n2o.find(Field)->second);
				if (collision) {
					obj->transform.position.y -= elapsed * paddle_speed;
				}
			}
			if (keystate[SDL_SCANCODE_DOWN]) {
				obj->transform.position.x += elapsed * paddle_speed;
				// Check collisions
				bool collision = false;
				collision |= s2s_collision(obj, n2o.find(P2)->second);
				collision |= s2s_collision(obj, n2o.find(D1)->second);
				collision |= s2s_collision(obj, n2o.find(D2)->second);
				collision |= b2s_collision(n2o.find(W1)->second, obj);
				collision |= b2s_collision(n2o.find(W2)->second, obj);
				collision |= exclusion(obj, n2o.find(Field)->second);
				if (collision) {
					obj->transform.position.x -= elapsed * paddle_speed;
				}
			}
			if (keystate[SDL_SCANCODE_LEFT]) {
				obj->transform.position.y -= elapsed * paddle_speed;
				// Check collisions
				bool collision = false;
				collision |= s2s_collision(obj, n2o.find(P2)->second);
				collision |= s2s_collision(obj, n2o.find(D1)->second);
				collision |= s2s_collision(obj, n2o.find(D2)->second);
				collision |= b2s_collision(n2o.find(W1)->second, obj);
				collision |= b2s_collision(n2o.find(W2)->second, obj);
				collision |= exclusion(obj, n2o.find(Field)->second);
				if (collision) {
					obj->transform.position.y += elapsed * paddle_speed;
				}

			}

			// Do state updates for paddle 2
			obj = n2o.find(P2)->second;
			// update the angle of rotation
			if (p2_dir == CW) {
				p2_angle -= elapsed * rotation_speed;
				obj->transform.rotation = glm::angleAxis(p2_angle,
					glm::vec3(0.0f, 0.0f, 1.0f));
			} else {
				p2_angle += elapsed * rotation_speed;
				obj->transform.rotation = glm::angleAxis(p2_angle,
					glm::vec3(0.0f, 0.0f, 1.0f));
			}
			// update the position
			// Check keystates
			keystate = SDL_GetKeyboardState(NULL);
			if (keystate[SDL_SCANCODE_W]) {
				obj->transform.position.x -= elapsed * paddle_speed;
				// Check collisions
				bool collision = false;
				collision |= s2s_collision(obj, n2o.find(P1)->second);
				collision |= s2s_collision(obj, n2o.find(D1)->second);
				collision |= s2s_collision(obj, n2o.find(D2)->second);
				collision |= b2s_collision(n2o.find(W1)->second, obj);
				collision |= b2s_collision(n2o.find(W2)->second, obj);
				collision |= exclusion(obj, n2o.find(Field)->second);
				if (collision) {
					obj->transform.position.x += elapsed * paddle_speed;
				}
			}
			if (keystate[SDL_SCANCODE_D]) {
				obj->transform.position.y += elapsed * paddle_speed;
				// Check collisions
				bool collision = false;
				collision |= s2s_collision(obj, n2o.find(P1)->second);
				collision |= s2s_collision(obj, n2o.find(D1)->second);
				collision |= s2s_collision(obj, n2o.find(D2)->second);
				collision |= b2s_collision(n2o.find(W1)->second, obj);
				collision |= b2s_collision(n2o.find(W2)->second, obj);
				collision |= exclusion(obj, n2o.find(Field)->second);
				if (collision) {
					obj->transform.position.y -= elapsed * paddle_speed;
				}
			}
			if (keystate[SDL_SCANCODE_S]) {
				obj->transform.position.x += elapsed * paddle_speed;
				// Check collisions
				bool collision = false;
				collision |= s2s_collision(obj, n2o.find(P1)->second);
				collision |= s2s_collision(obj, n2o.find(D1)->second);
				collision |= s2s_collision(obj, n2o.find(D2)->second);
				collision |= b2s_collision(n2o.find(W1)->second, obj);
				collision |= b2s_collision(n2o.find(W2)->second, obj);
				collision |= exclusion(obj, n2o.find(Field)->second);
				if (collision) {
					obj->transform.position.x -= elapsed * paddle_speed;
				}
			}
			if (keystate[SDL_SCANCODE_A]) {
				obj->transform.position.y -= elapsed * paddle_speed;
				// Check collisions
				bool collision = false;
				collision |= s2s_collision(obj, n2o.find(P1)->second);
				collision |= s2s_collision(obj, n2o.find(D1)->second);
				collision |= s2s_collision(obj, n2o.find(D2)->second);
				collision |= b2s_collision(n2o.find(W1)->second, obj);
				collision |= b2s_collision(n2o.find(W2)->second, obj);
				collision |= exclusion(obj, n2o.find(Field)->second);
				if (collision) {
					obj->transform.position.y += elapsed * paddle_speed;
				}
			}

			// ball movements
			{
				obj = n2o.find(Ball)->second;
				// ball collision with wall 1
				if (b2s_collision(n2o.find(W1)->second, obj)) {
					ball_velocity.x = -ball_velocity.x;
				}
				if (b2s_collision(n2o.find(W2)->second, obj)) {
					ball_velocity.x = -ball_velocity.x;
				}
				Scene::Object *temp = n2o.find(D1)->second;
				if (s2s_collision(obj, temp)) {
					reflect(ball_velocity, 
							glm::vec2(obj->transform.position - temp->transform.position));
				}
				temp = n2o.find(D2)->second;
				if (s2s_collision(obj, temp)) {
					reflect(ball_velocity, 
							glm::vec2(obj->transform.position - temp->transform.position));
				}
				temp = n2o.find(P1)->second;
				glm::vec2 normal;
				if (s2p_collision(obj, temp, p1_angle, p1_dir, normal)) {
					knock(ball_velocity, normal);
				}
				temp = n2o.find(P2)->second;
				if (s2p_collision(obj, temp, p2_angle, p2_dir, normal)) {
					knock(ball_velocity, normal);
				}
				obj->transform.position.x += ball_velocity.x * elapsed;
				obj->transform.position.y += ball_velocity.y * elapsed;

				temp = n2o.find(Field)->second;
				//victory conditions
				if (obj->transform.position.y > temp->transform.position.y + temp->dimension.y / 2) {
					player2_win = true;
					game_over = true;
				} else if (obj->transform.position.y < temp->transform.position.y - temp->dimension.y / 2) {
					player1_win = true;
					game_over = true;
				}
			}

			if (game_over) {
				if (player1_win) {
					std::cout << "player1 wins" << std::endl;
				} else if (player2_win) {
					std::cout << "player2 wins" << std::endl;
				}
			}

			//camera:
			scene.camera.transform.position = camera.radius * glm::vec3(
				std::cos(camera.elevation) * std::cos(camera.azimuth),
				std::cos(camera.elevation) * std::sin(camera.azimuth),
				std::sin(camera.elevation)) + camera.target;

			glm::vec3 out = -glm::normalize(camera.target - scene.camera.transform.position);
			glm::vec3 up = glm::vec3(0.0f, 0.0f, 1.0f);
			up = glm::normalize(up - glm::dot(up, out) * out);
			glm::vec3 right = glm::cross(up, out);
			
			scene.camera.transform.rotation = glm::quat_cast(
				glm::mat3(right, up, out)
			);
			scene.camera.transform.scale = glm::vec3(1.0f, 1.0f, 1.0f);

			if (game_over) {
				sleep(1);
			}
		}

		//draw output:
		glClearColor(0.5, 0.5, 0.5, 0.0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);


		{ //draw game state:
			glUseProgram(program);
			glUniform3fv(program_to_light, 1, glm::value_ptr(glm::normalize(glm::vec3(0.0f, 1.0f, 10.0f))));
			scene.render();
		}


		SDL_GL_SwapWindow(window);
	}


	//------------  teardown ------------

	SDL_GL_DeleteContext(context);
	context = 0;

	SDL_DestroyWindow(window);
	window = NULL;

	return 0;
}



static GLuint compile_shader(GLenum type, std::string const &source) {
	GLuint shader = glCreateShader(type);
	GLchar const *str = source.c_str();
	GLint length = source.size();
	glShaderSource(shader, 1, &str, &length);
	glCompileShader(shader);
	GLint compile_status = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compile_status);
	if (compile_status != GL_TRUE) {
		std::cerr << "Failed to compile shader." << std::endl;
		GLint info_log_length = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_log_length);
		std::vector< GLchar > info_log(info_log_length, 0);
		GLsizei length = 0;
		glGetShaderInfoLog(shader, info_log.size(), &length, &info_log[0]);
		std::cerr << "Info log: " << std::string(info_log.begin(), info_log.begin() + length);
		glDeleteShader(shader);
		throw std::runtime_error("Failed to compile shader.");
	}
	return shader;
}

static GLuint link_program(GLuint fragment_shader, GLuint vertex_shader) {
	GLuint program = glCreateProgram();
	glAttachShader(program, vertex_shader);
	glAttachShader(program, fragment_shader);
	glLinkProgram(program);
	GLint link_status = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &link_status);
	if (link_status != GL_TRUE) {
		std::cerr << "Failed to link shader program." << std::endl;
		GLint info_log_length = 0;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &info_log_length);
		std::vector< GLchar > info_log(info_log_length, 0);
		GLsizei length = 0;
		glGetProgramInfoLog(program, info_log.size(), &length, &info_log[0]);
		std::cerr << "Info log: " << std::string(info_log.begin(), info_log.begin() + length);
		throw std::runtime_error("Failed to link program");
	}
	return program;
}

bool s2s_collision(Scene::Object *obj1,Scene::Object *obj2) {
	float x = obj1->transform.position.x - obj2->transform.position.x;
	float y = obj1->transform.position.y - obj2->transform.position.y;

	float dist = std::sqrt(x * x + y * y);

	if (dist < obj1->radius.x + obj2->radius.x) {
		return true;
	}
	return false;
}

bool b2s_collision(Scene::Object *block,Scene::Object *sphere) {
	if ((block->transform.position.x + block->dimension.x / 2
			> sphere->transform.position.x - sphere->radius.x)
		&& (block->transform.position.x - block->dimension.x / 2
			< sphere->transform.position.x + sphere->radius.x)
		&& (block->transform.position.y + block->dimension.y / 2
			> sphere->transform.position.y - sphere->radius.y)
		&& (block->transform.position.y - block->dimension.y / 2
			< sphere->transform.position.y + sphere->radius.y)) {
		return true;
	}
	return false;
}

bool exclusion(Scene::Object *obj1, Scene::Object *obj2) {
	if (obj1->transform.position.x < obj2->transform.position.x + obj2->dimension.x / 2
			&& obj1->transform.position.x > obj2->transform.position.x - obj2->dimension.x / 2
			&& obj1->transform.position.y < obj2->transform.position.y + obj2->dimension.y / 2
			&& obj1->transform.position.y > obj2->transform.position.y - obj2->dimension.y / 2) {
		return false;
	}

	return true;
}

bool s2p_collision(Scene::Object *obj, Scene::Object *temp, float theta, bool dir, glm::vec2 &normal) {
	glm::vec2 v1 = glm::vec2(obj->transform.position - temp->transform.position);
	glm::vec2 v2 = glm::vec2(temp->radius.x * std::cos(theta), temp->radius.x * std::sin(theta));
	normalize(v2);
	float r = dot(v1, v2);
	if ( r < 0 || r > temp->radius.x + obj->radius.x) {
		return false;
	}
	glm::vec2 req = v1 - r * v2;
	float height = std::sqrt(dot(req, req));
	if (dir) {
		normal.x = req.y;
		normal.y = -req.x;
	} else {
		normal.x = -req.y;
		normal.y = req.x;
	}

	if (height < obj->radius.x + temp->dimension.y / 2) {
		return true;
	}
	return false;
}

void knock(glm::vec2 &ball_velocity, glm::vec2 normal) {
	normalize(normal);
	ball_velocity = 10.0f * normal;
}

void reflect(glm::vec2 &ball_velocity, glm::vec2 normal) {
	reflect(ball_velocity, normal, 5.0f);
}

void reflect(glm::vec2 &ball_velocity, glm::vec2 normal, float damp) {
	normalize(normal);
	glm::vec2 neg = -ball_velocity;
	float angle = std::acos(dot(neg, normal) / length(neg));
	float y = 5.0f * std::cos(angle);
	float x = -std::sin(angle) * length(neg);
	ball_velocity.x = x;
	ball_velocity.y = y;
}
