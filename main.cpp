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

static GLuint compile_shader(GLenum type, std::string const &source);
static GLuint link_program(GLuint vertex_shader, GLuint fragment_shader);

static const int NUM_PNG = 10;

static const std::string PNG_LIST[NUM_PNG] = {
	"balloon1.png",
	"balloon2.png",
	"balloon3.png",
	"stand.png",
	"base.png",
	"link1.png",
	"link2.png",
	"link3.png",
	"crate.png",
	"cube.png",
};

static const std::string B1 = std::string("Balloon1");
static const std::string B2 = std::string("Balloon2");
static const std::string B3 = std::string("Balloon3");
static const std::string BASE = std::string("Base");
static const std::string STAND = std::string("Stand");
static const std::string LINK1 = std::string("Link1");
static const std::string LINK2 = std::string("Link2");
static const std::string LINK3 = std::string("Link3");

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

	//texture:
	GLuint tex[NUM_PNG];
	glm::uvec2 tex_size[NUM_PNG];
	for (int i = 0; i < NUM_PNG; i++) {
		tex_size[i] = glm::uvec2(0, 0);
	}

	{ //load textures : 'This is going to be super dirty
		std::vector< uint32_t > data[NUM_PNG];
		//std::vector< uint32_t > data;

		//create a texture object:
		glGenTextures(NUM_PNG, tex);

		for (int i = 0; i < NUM_PNG; i++) {
			if (!load_png(PNG_LIST[i], &tex_size[i].x, &tex_size[i].y, &data[i], LowerLeftOrigin)) {
				std::cerr << "Failed to load texture " << PNG_LIST[i] << std::endl;
				exit(1);
			}
			
			glActiveTexture(GL_TEXTURE0 + i);
			//bind texture object to GL_TEXTURE_2D:
			glBindTexture(GL_TEXTURE_2D, tex[i]);

			//upload texture data from data:
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex_size[i].x, tex_size[i].y, 0, GL_RGBA, GL_UNSIGNED_BYTE, &data[i][0]);

			std::cout << "i = " << i << " tex[i] = " << tex[i] << std::endl;
			//set texture sampling parameters:
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		}

	}

	//shader program:
	GLuint program = 0;
	GLuint program_Position = 0;
	GLuint program_Normal = 0;
	GLuint program_UVCoord = 0;
	GLuint program_mvp = 0;
	GLuint program_itmv = 0;
	GLuint program_to_light = 0;
	GLuint program_tex = 0;
	{ //compile shader program:
		GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER,
			"#version 330\n"
			"uniform mat4 mvp;\n"
			"uniform mat3 itmv;\n"
			"in vec4 Position;\n"
			"in vec3 Normal;\n"
			"in vec2 UVCoord;\n"
			"out vec3 normal;\n"
			"out vec2 uvcoord;\n"
			"void main() {\n"
			"	gl_Position = mvp * Position;\n"
			"	normal = itmv * Normal;\n"
			"	uvcoord = UVCoord;\n"
			"}\n"
		);

		GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER,
			"#version 330\n"
			"uniform vec3 to_light;\n"
			"uniform sampler2D tex;\n"
			"in vec3 normal;\n"
			"in vec2 uvcoord;\n"
			"out vec4 fragColor;\n"
			"void main() {\n"
			"	float light = max(0.0, dot(normalize(normal), to_light));\n"
			"	vec4 color = texture(tex, uvcoord);\n"
			"	float alpha = color.w;\n"
			"	fragColor = vec4(light * vec3(color), alpha);\n"
			//"	fragColor = color;\n"
			"}\n"
		);

		program = link_program(fragment_shader, vertex_shader);

		//look up attribute locations:
		program_Position = glGetAttribLocation(program, "Position");
		if (program_Position == -1U) throw std::runtime_error("no attribute named Position");
		program_Normal = glGetAttribLocation(program, "Normal");
		if (program_Normal == -1U) throw std::runtime_error("no attribute named Normal");
		program_UVCoord = glGetAttribLocation(program, "UVCoord");
		if (program_UVCoord == -1U) throw std::runtime_error("no attribute named UVCoord");

		//look up uniform locations:
		program_mvp = glGetUniformLocation(program, "mvp");
		if (program_mvp == -1U) throw std::runtime_error("no uniform named mvp");
		program_itmv = glGetUniformLocation(program, "itmv");
		if (program_itmv == -1U) throw std::runtime_error("no uniform named itmv");

		program_to_light = glGetUniformLocation(program, "to_light");
		if (program_to_light == -1U) throw std::runtime_error("no uniform named to_light");
		program_tex = glGetUniformLocation(program, "tex");
		if (program_tex == -1U) throw std::runtime_error("no uniform named tex");
	}

	//--------- Game constants -------
	
	std::map <std::string, int> n2id; // name to index map
	n2id["Balloon1"] = 0;
	n2id["Balloon2"] = 1;
	n2id["Balloon3"] = 2;
	n2id["Stand"]	 = 3;
	n2id["Base"]	 = 4;
	n2id["Link1"]	 = 5;
	n2id["Link2"]	 = 6;
	n2id["Link3"]	 = 7;
	n2id["Crate"] 	 = 9;	// giving crate cube as it's texture is invisible
	n2id["Cube.001"] = 9;
	n2id["Crate.001"] = 9;
	n2id["Crate.002"] = 9;
	n2id["Crate.003"] = 9;
	n2id["Crate.004"] = 9;
	n2id["Crate.005"] = 9;
	n2id["Balloon1-Pop"] = 0;
	n2id["Balloon2-Pop"] = 1;
	n2id["Balloon3-Pop"] = 2;

	std::map <std::string, Scene::Object*> n2o;

	//------------ meshes ------------

	Meshes meshes;

	std::cerr << "Loading meshes!" << std::endl;
	{ //add meshes to database:
		Meshes::Attributes attributes;
		attributes.Position = program_Position;
		attributes.Normal = program_Normal;
		attributes.UVCoord = program_UVCoord;

		meshes.load("meshes.blob", attributes);
	}

	std::cerr << "Successfully loaded the meshes!" << std::endl;
	
	//------------ scene ------------

	Scene scene;
	//set up camera parameters based on window:
	scene.camera.fovy = glm::radians(80.0f);
	scene.camera.aspect = float(config.size.x) / float(config.size.y);
	scene.camera.near = 0.01f;
	//(transform will be handled in the update function below)

	//add some objects from the mesh library:
	auto add_object = [&](std::string const &name, glm::vec3 const &position, glm::quat const &rotation, glm::vec3 const &scale, int &index, GLuint &tex, glm::vec3 const &dimension) -> Scene::Object & {
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
		object.program_tex = program_tex;
		object.tex = tex;
		object.texture_used = index;
		object.dimension = dimension;
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

			std::cout << "scn0 accessed" << std::endl;

			std::vector< SceneEntry > data;
			read_chunk(file, "scn0", &data);

			for (auto const &entry : data) {
				if (!(entry.name_begin <= entry.name_end && entry.name_end <= strings.size())) {
					throw std::runtime_error("index entry has out-of-range name begin/end");
				}
				std::string name(&strings[0] + entry.name_begin, &strings[0] + entry.name_end);
				int index = n2id.find(name)->second;
				std::cout << name << " " << index << " " << tex[index] << std::endl;
				add_object(name, entry.position, entry.rotation, entry.scale, index, tex[index], entry.dimension);
			}
		}
	}

	/*
	//create a weird waving tree stack:
	std::vector< Scene::Object * > tree_stack;
	tree_stack.emplace_back( &add_object("Tree", glm::vec3(1.0f, 0.0f, 0.2f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(0.3f)) );
	tree_stack.emplace_back( &add_object("Tree", glm::vec3(0.0f, 0.0f, 1.7f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(0.9f)) );
	tree_stack.emplace_back( &add_object("Tree", glm::vec3(0.0f, 0.0f, 1.7f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(0.9f)) );
	tree_stack.emplace_back( &add_object("Tree", glm::vec3(0.0f, 0.0f, 1.7f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(0.9f)) );

	for (uint32_t i = 1; i < tree_stack.size(); ++i) {
		tree_stack[i]->transform.set_parent(&tree_stack[i-1]->transform);
	}

	std::vector< float > wave_acc(tree_stack.size(), 0.0f);
	*/

	glm::vec2 mouse = glm::vec2(0.0f, 0.0f); //mouse position in [-1,1]x[-1,1] coordinates

	struct {
		float radius = 5.0f;
		float elevation = 0.0f;
		float azimuth = 0.0f;
		glm::vec3 target = glm::vec3(0.0f, 0.0f, 0.0f);
	} camera;


	//------------ game state -----------
	
	int number_of_balloons = 3;
	bool balloon_dir[number_of_balloons];

	//------------ game loop ------------

	bool should_quit = false;
	float theta = 0.0f;

	while (true) {
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
			} else if (evt.type == SDL_KEYDOWN && evt.key.keysym.sym == SDLK_ESCAPE) {
				should_quit = true;
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

			// update balloon positions
			Scene::Object *obj;

			obj = n2o.find(B1)->second;
			if (obj->transform.position.z - obj->dimension.z < -0.5f) {
				balloon_dir[0] = true;
			} else if (obj->transform.position.z - obj->dimension.z / 2 > 5.0f) {
				balloon_dir[0] = false;
			}
			obj->transform.position.z += (balloon_dir[0] ? 1 : -1) * elapsed * 1.0;

			obj = n2o.find(B2)->second;
			if (obj->transform.position.z - obj->dimension.z < -0.5f) {
				balloon_dir[1] = true;
			} else if (obj->transform.position.z - obj->dimension.z / 2 > 5.0f) {
				balloon_dir[1] = false;
			}
			obj->transform.position.z += (balloon_dir[1] ? 1 : -1) * elapsed * 1.0;

			obj = n2o.find(B3)->second;
			if (obj->transform.position.z - obj->dimension.z < -0.5f) {
				balloon_dir[2] = true;
			} else if (obj->transform.position.z - obj->dimension.z / 2 > 5.0f) {
				balloon_dir[2] = false;
			}
			obj->transform.position.z += (balloon_dir[2] ? 1 : -1) * elapsed * 1.0;
			

			auto *keystate = SDL_GetKeyboardState(NULL);
			if (keystate[SDL_SCANCODE_Z]) {
				theta += elapsed * 0.2f;
				theta -= std::floor(theta);
				obj = n2o.find(LINK3)->second;
				obj->transform.rotation = glm::angleAxis(std::cos(theta),
					glm::vec3(0.0f, 1.0f, 0.0f));
			}

			if (keystate[SDLK_x]) {

			}

			if (keystate[SDLK_a]) {

			}

			if (keystate[SDLK_s]) {

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
