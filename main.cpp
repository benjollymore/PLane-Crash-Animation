#include <OpenGP/GL/Application.h>
#include <OpenGP/external/LodePNG/lodepng.cpp>
#include <random>
#include <stdlib.h>

#define FLYTIME 8.75
#define NUMSHARDS 150
#define RUNTIME 15
#define POINTSIZE 10.0f

using namespace OpenGP;

const int width = 1200, height = 800;
typedef Eigen::Transform<float, 3, Eigen::Affine> Transform;

const char* line_vshader =
#include "line_vshader.glsl"
;

const char* line_fshader =
#include "line_fshader.glsl"
;

const char* fb_vshader =
#include "fb_vshader.glsl"
;
const char* fb_fshader =
#include "fb_fshader.glsl"
;
const char* quad_vshader =
#include "quad_vshader.glsl"
;
const char* quad_fshader =
#include "quad_fshader.glsl"
;

const float SpeedFactor = 1;
void init();
void quadInit(std::unique_ptr<GPUMesh>& quad);
void loadTexture(std::unique_ptr<RGBA8Texture>& texture, const char* filename);
void drawScene(float timeCount);

std::unique_ptr<GPUMesh> quad;

std::unique_ptr<Shader> quadShader;
std::unique_ptr<Shader> fbShader;

std::unique_ptr<RGBA8Texture> prop;
std::unique_ptr<RGBA8Texture> plane;
std::unique_ptr<RGBA8Texture> backdrop;
std::unique_ptr<RGBA8Texture> crash;
std::unique_ptr<RGBA8Texture> fin;

std::unique_ptr<RGBA8Texture> shard1;
std::unique_ptr<RGBA8Texture> shard2;
std::unique_ptr<RGBA8Texture> shard3;
std::unique_ptr<RGBA8Texture> shard4;
std::unique_ptr<RGBA8Texture> shard5;
//std::unique_ptr<RGBA8Texture> drop;

std::unique_ptr<Framebuffer> fb;
std::unique_ptr<RGBA8Texture> c_buf;

std::unique_ptr<Shader> lineShader;
std::unique_ptr<GPUMesh> line;

std::unique_ptr<GPUMesh> bezierCurve;
std::vector<Vec2> bezierApproxPoints;
std::vector<Vec2> controlPoints;

std::vector<Vec4> shardContainer;
//std::vector<Vec3> dropContainer;

/**	
	calc bezier curve value at a given point using trapezoid decomp.
	fixed 4 control points so doing it iteratively 
	but should probably be doing this recursively.
*/
Vec2 calcBezierPoint(float lineParam) {
	//calc lines between control vectors
	Vec2 l1 = controlPoints[1] - controlPoints[0];
	Vec2 l2 = controlPoints[2] - controlPoints[1];
	Vec2 l3 = controlPoints[3] - controlPoints[2];

	//first round of decomp, calc new points
	Vec2 pt1 = controlPoints[0] + lineParam * l1;
	Vec2 pt2 = controlPoints[1] + lineParam * l2;
	Vec2 pt3 = controlPoints[2] + lineParam * l3;
	//calc new lines. 
	//save some memory, just overwrite the ones from before
	l1 = pt2 - pt1;
	l2 = pt3 - pt2;
	
	//next round of decomp, calc new points
	pt1 = pt1 + lineParam * l1;
	pt2 = pt2 + lineParam * l2;
	//calc new line
	l1 = pt2 - pt1;

	//return point on bezier curve
	return (pt1 + lineParam * l1);
}

int main(int, char**) {

	srand(time(NULL));

	Application app;
	init();

	/// TODO: initialize framebuffer
	fb = std::unique_ptr<Framebuffer>(new Framebuffer());

	/// TODO: initialize color buffer texture, and allocate memory
	c_buf = std::unique_ptr<RGBA8Texture>(new RGBA8Texture());
	c_buf->allocate(width, height);

	/// TODO: attach color texture to framebuffer
	fb->attach_color_texture(*c_buf);

	Window& window = app.create_window([&](Window&) {
		glViewport(0, 0, width, height);

		/// TODO: First draw the scene onto framebuffer
		/// bind and then unbind framebuffer
		fb->bind();
		glClear(GL_COLOR_BUFFER_BIT);
		drawScene(glfwGetTime());
		fb->unbind();

		/// Render to Window, uncomment the lines and do TODOs
		glViewport(0, 0, width, height);
		glClear(GL_COLOR_BUFFER_BIT);
		fbShader->bind();
		/// TODO: Bind texture and set uniforms
		glActiveTexture(GL_TEXTURE0);
		c_buf->bind();
		fbShader->set_uniform("tex", 0);
		fbShader->set_uniform("tex_width", float(width));
		fbShader->set_uniform("tex_height", float(height));
		quad->set_attributes(*fbShader);
		quad->draw();

		c_buf->unbind();
		fbShader->unbind();

	});

	window.set_title("FrameBuffer");
	window.set_size(width, height);

	// Mouse position and selected point
	Vec2 position = Vec2(0, 0);
	Vec2 *selection = nullptr;

	// Display callback
	Window& window2 = app.create_window([&](Window&) {
		glViewport(0, 0, width, height);
		glClear(GL_COLOR_BUFFER_BIT);
		glPointSize(POINTSIZE);

		lineShader->bind();

		// Draw line red
		lineShader->set_uniform("selection", -1);
		line->set_attributes(*lineShader);
		line->set_mode(GL_LINE_STRIP);
		line->draw();

		bezierCurve->set_attributes(*lineShader);
		bezierCurve->set_mode(GL_LINE_STRIP);
		bezierCurve->draw();

		// Draw points red and selected point blue
		if (selection != nullptr) lineShader->set_uniform("selection", int(selection - &controlPoints[0]));
		line->set_mode(GL_POINTS);
		line->draw();

		lineShader->unbind();
	});

	window2.set_title("Mouse");
	window2.set_size(width, height);

	// Mouse movement callback
	window2.add_listener<MouseMoveEvent>([&](const MouseMoveEvent &m) {
		// Mouse position in clip coordinates
		Vec2 p = 2.0f*(Vec2(m.position.x() / width, -m.position.y() / height) - Vec2(0.5f, -0.5f));
		if (selection && (p - position).norm() > 0.0f) {
			//do dtuff.. but not likely
		}
		position = p;
	});

	// Mouse click callback
	window2.add_listener<MouseButtonEvent>([&](const MouseButtonEvent &e) {
		// Mouse selection case
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && !e.released) {
			selection = nullptr;
			for (auto&& v : controlPoints) {
				if ((v - position).norm() < POINTSIZE / std::min(width, height)) {
					selection = &v;
					break;
				}
			}
		}
		// Mouse release case
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.released) {
			if (selection) {
				bezierApproxPoints.clear();
				selection->x() = position.x();
				selection->y() = position.y();
				selection = nullptr;
				line->set_vbo<Vec2>("vposition", controlPoints);
				for (int i = 0; i < 4; i++) {
					std::cout << controlPoints[i].x() << " " << controlPoints[i].y() << std::endl;;
				}
				std::cout << "------------------" << std::endl;
				for (float i = 0; i < 100; i++) {
					bezierApproxPoints.push_back(calcBezierPoint(i / 100));
				}
				bezierCurve->set_vbo<Vec2>("vposition", bezierApproxPoints);
				glfwSetTime(0);
			}

		}
	});

	return app.run();
}

void init() {
	glClearColor(1, 1, 1, /*solid*/1.0);

	fbShader = std::unique_ptr<Shader>(new Shader());
	fbShader->verbose = true;
	fbShader->add_vshader_from_source(fb_vshader);
	fbShader->add_fshader_from_source(fb_fshader);
	fbShader->link();

	//init shader for lines
	quadShader = std::unique_ptr<Shader>(new Shader());
	quadShader->verbose = true;
	quadShader->add_vshader_from_source(quad_vshader);
	quadShader->add_fshader_from_source(quad_fshader);
	quadShader->link();

	quadInit(quad);

	//load plane and background textures
	loadTexture(prop, "prop.png");
	loadTexture(backdrop, "night.png");
	loadTexture(plane, "plane.png");
	loadTexture(crash, "crash.png");
	loadTexture(fin, "fin.png");

	//load particle textures
	loadTexture(shard1, "shard1.png");
	loadTexture(shard2, "shard2.png");
	loadTexture(shard3, "shard3.png");
	loadTexture(shard4, "shard4.png");
	loadTexture(shard5, "shard5.png");
	//loadTexture(drop, "drop.png");

	glClearColor(1, 1, 1, /*solid*/1.0);

	lineShader = std::unique_ptr<Shader>(new Shader());
	lineShader->verbose = true;
	lineShader->add_vshader_from_source(line_vshader);
	lineShader->add_fshader_from_source(line_fshader);
	lineShader->link();

	//init original control points
	controlPoints = std::vector<Vec2>();
	controlPoints.push_back(Vec2(-0.805f, 0.785f));
	controlPoints.push_back(Vec2(-0.738f, 0.0175f));
	controlPoints.push_back(Vec2(0.92f, 0.4125f));
	controlPoints.push_back(Vec2(-0.678f, -0.4525f));

	//init first control points and lines between them
	line = std::unique_ptr<GPUMesh>(new GPUMesh());
	line->set_vbo<Vec2>("vposition", controlPoints);
	std::vector<unsigned int> indices = { 0,1,2,3 };
	line->set_triangles(indices);

	//init first bezier curve
	std::vector<unsigned int> bigUglyIndicies;
	bezierCurve = std::unique_ptr<GPUMesh>(new GPUMesh());
	for (float i = 0; i < 100; i++) {
		bezierApproxPoints.push_back(calcBezierPoint(i / 100));
		bigUglyIndicies.push_back(unsigned int(i));
	}
	bezierCurve->set_vbo<Vec2>("vposition", bezierApproxPoints);
	bezierCurve->set_triangles(bigUglyIndicies);

	//create random directioned glass shards approximately originating from where camera is impacted
	for (int i = 0; i < NUMSHARDS; i++) {
		shardContainer.push_back(
			Vec4(
				-0.25f + 0.01*(((float(rand()) / float(RAND_MAX)) * (2)) - 1), 
				-0.35f + 0.01*(((float(rand()) / float(RAND_MAX)) * (2)) - 1),
				((float(rand()) / float(RAND_MAX)) * (2)) - 1,
				((float(rand()) / float(RAND_MAX)) * (2)) - 1
			)
		);
	}

	
}

void quadInit(std::unique_ptr<GPUMesh>& quad) {
	quad = std::unique_ptr<GPUMesh>(new GPUMesh());
	std::vector<Vec3> quad_vposition = {
		Vec3(-1, -1, 0),
		Vec3(-1,  1, 0),
		Vec3(1, -1, 0),
		Vec3(1,  1, 0)
	};
	quad->set_vbo<Vec3>("vposition", quad_vposition);
	std::vector<unsigned int> quad_triangle_indices = {
		0, 2, 1, 1, 2, 3
	};
	quad->set_triangles(quad_triangle_indices);
	std::vector<Vec2> quad_vtexcoord = {
		Vec2(0, 0),
		Vec2(0,  1),
		Vec2(1, 0),
		Vec2(1,  1)
	};
	quad->set_vtexcoord(quad_vtexcoord);
}

void loadTexture(std::unique_ptr<RGBA8Texture>& texture, const char* filename) {
	// Used snippet from https://raw.githubusercontent.com/lvandeve/lodepng/master/examples/example_decode.cpp
	std::vector<unsigned char> image; //the raw pixels
	unsigned width, height;
	//decode
	unsigned error = lodepng::decode(image, width, height, filename);
	//if there's an error, display it
	if (error) std::cout << "decoder error " << error << ": " << lodepng_error_text(error) << std::endl;
	//the pixels are now in the vector "image", 4 bytes per pixel, ordered RGBARGBA..., use it as texture, draw it, ...

	// unfortunately they are upside down...lets fix that
	unsigned char* row = new unsigned char[4 * width];
	for (int i = 0; i < int(height) / 2; ++i) {
		memcpy(row, &image[4 * i * width], 4 * width * sizeof(unsigned char));
		memcpy(&image[4 * i * width], &image[image.size() - 4 * (i + 1) * width], 4 * width * sizeof(unsigned char));
		memcpy(&image[image.size() - 4 * (i + 1) * width], row, 4 * width * sizeof(unsigned char));
	}
	delete row;

	texture = std::unique_ptr<RGBA8Texture>(new RGBA8Texture());
	texture->upload_raw(width, height, &image[0]);
}

void drawScene(float timeCount)
{

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	//init transformation vectors
	Transform plane1Trans = Transform::Identity();
	Transform plane2Trans = Transform::Identity();
	Transform shardTrans = Transform::Identity();
	//Transform dropTrans = Transform::Identity();

	float t = timeCount * SpeedFactor;
	std::cout << t << std::endl;

	/*
	Code for raindrops falling. Never ended up getting this working quite 
	right, decided to omit from final submission.

	//initialize one random raindrop
	dropContainer.push_back(Vec3(
		((float(rand()) / float(RAND_MAX)) * (2)) - 1,
		1.0f,
		((float(rand()) / float(RAND_MAX))
	)));

	for (int i = 0; i < dropContainer.size(); i++) {

		dropTrans = Eigen::Translation3f(1 - .015*dropContainer[i].z()*t,
			dropContainer[i].x(), 0);
		dropContainer[i].y() -= .015*dropContainer[i].z()*t;
		dropTrans *= Eigen::AlignedScaling3f(.02f, 0.01f, 1);

		std::cout << " dropping boyz" << std::endl;
		quadShader->bind();
		quadShader->set_uniform("M", dropTrans.matrix());
		// Make texture unit 0 active
		glActiveTexture(GL_TEXTURE0);
		// Bind the texture to the active unit for drawing
		drop->bind();
		// Set the shader's texture uniform to the index of the texture unit we have
		// bound the texture to
		quadShader->set_uniform("tex", 0);
		quad->set_attributes(*quadShader);
		quad->draw();
		drop->unbind();
		quadShader->unbind();
	
	}
	*/


	/*
	Transformations during flight
	*/
	if (t <= FLYTIME) {
		quadShader->bind();
		quadShader->set_uniform("M", plane1Trans.matrix());
		// Make texture unit 0 active
		glActiveTexture(GL_TEXTURE0);
		// Bind the texture to the active unit for drawing
		backdrop->bind();
		// Set the shader's texture uniform to the index of the texture unit we have
		// bound the texture to
		quadShader->set_uniform("tex", 0);
		quad->set_attributes(*quadShader);
		quad->draw();
		backdrop->unbind();

		/*
		Transformations for Plane 1
		*/
		Vec2 bezierTranslation = calcBezierPoint(t / 10);
		plane1Trans *= Eigen::Translation3f(bezierTranslation.x(), bezierTranslation.y(), 1);
		plane1Trans *= Eigen::AngleAxisf(t + M_PI / 4, Eigen::Vector3f::UnitZ());
		plane1Trans *= Eigen::AlignedScaling3f(.04f + .15* t, 0.02f + .1*t, 1);

		quadShader->bind();
		quadShader->set_uniform("M", plane1Trans.matrix());
		// Make texture unit 0 active
		glActiveTexture(GL_TEXTURE0);
		// Bind the texture to the active unit for drawing
		plane->bind();
		// Set the shader's texture uniform to the index of the texture unit we have
		// bound the texture to
		quadShader->set_uniform("tex", 0);
		quad->set_attributes(*quadShader);
		quad->draw();
		plane->unbind();
		quadShader->unbind();

		plane1Trans *= Eigen::Translation3f(0, 0.07, 0);
		plane1Trans *= Eigen::AlignedScaling3f(.5f, 1.0f, 1);
		plane1Trans *= Eigen::AngleAxisf(7 * t + M_PI / 2, Eigen::Vector3f::UnitZ());

		quadShader->bind();
		quadShader->set_uniform("M", plane1Trans.matrix());
		// Make texture unit 0 active
		glActiveTexture(GL_TEXTURE0);
		// Bind the texture to the active unit for drawing
		prop->bind();
		// Set the shader's texture uniform to the index of the texture unit we have
		// bound the texture to
		quadShader->set_uniform("tex", 0);
		quad->set_attributes(*quadShader);
		quad->draw();
		prop->unbind();
		quadShader->unbind();


		/*
		Transformations for plane 2
		*/
		plane2Trans *= Eigen::Translation3f(0.1f + .2*t, 0.1 + .2*t, 1);
		plane2Trans *= Eigen::AlignedScaling3f(.02f + .3* t, 0.01f + .2*t, 1);

		quadShader->bind();
		quadShader->set_uniform("M", plane2Trans.matrix());
		// Make texture unit 0 active
		glActiveTexture(GL_TEXTURE0);
		// Bind the texture to the active unit for drawing
		plane->bind();
		// Set the shader's texture uniform to the index of the texture unit we have
		// bound the texture to
		quadShader->set_uniform("tex", 0);
		quad->set_attributes(*quadShader);
		quad->draw();
		plane->unbind();
		quadShader->unbind();

		plane2Trans *= Eigen::Translation3f(0, 0.07, 0);
		plane2Trans *= Eigen::AlignedScaling3f(.5f, 1.0f, 1);
		plane2Trans *= Eigen::AngleAxisf(7 * t + M_PI / 2, Eigen::Vector3f::UnitZ());

		quadShader->bind();
		quadShader->set_uniform("M", plane2Trans.matrix());
		// Make texture unit 0 active
		glActiveTexture(GL_TEXTURE0);
		// Bind the texture to the active unit for drawing
		prop->bind();
		// Set the shader's texture uniform to the index of the texture unit we have
		// bound the texture to
		quadShader->set_uniform("tex", 0);
		quad->set_attributes(*quadShader);
		quad->draw();
		prop->unbind();
		quadShader->unbind();
	}

	/*
	End animation
	*/
	else if (t > RUNTIME) {
		quadShader->bind();
		quadShader->set_uniform("M", shardTrans.matrix());
		// Make texture unit 0 active
		glActiveTexture(GL_TEXTURE0);
		// Bind the texture to the active unit for drawing
		fin->bind();
		// Set the shader's texture uniform to the index of the texture unit we have
		// bound the texture to
		quadShader->set_uniform("tex", 0);
		quad->set_attributes(*quadShader);
		quad->draw();
		fin->unbind();
	}

	/*
	Transformations for broken glass after plane impacts camera
	*/
	else {
		quadShader->bind();
		quadShader->set_uniform("M", shardTrans.matrix());
		// Make texture unit 0 active
		glActiveTexture(GL_TEXTURE0);
		// Bind the texture to the active unit for drawing
		crash->bind();
		// Set the shader's texture uniform to the index of the texture unit we have
		// bound the texture to
		quadShader->set_uniform("tex", 0);
		quad->set_attributes(*quadShader);
		quad->draw();
		crash->unbind();
		
		//Apply transformation to each shard
		for (int i = 0; i < NUMSHARDS; i++) {
			shardTrans = Eigen::Translation3f(shardContainer[i].x() + .015*shardContainer[i].z()*(t - FLYTIME), 
				shardContainer[i].y() + .015*shardContainer[i].w()*(t - FLYTIME), 0);
			shardContainer[i].x() += .015*shardContainer[i].z()*(t - FLYTIME);
			shardContainer[i].y() += .015*shardContainer[i].w()*(t - FLYTIME);

			shardTrans *= Eigen::AngleAxisf(t + (M_PI /4) *  shardContainer[i].w(), Eigen::Vector3f::UnitZ());
			shardTrans *= Eigen::AlignedScaling3f(.02f + .1*shardContainer[i].w()*(t - FLYTIME), 0.02f + .1*shardContainer[i].w()*(t - FLYTIME), 1);

			quadShader->bind();
			quadShader->set_uniform("M", shardTrans.matrix());
			// Make texture unit 0 active
			glActiveTexture(GL_TEXTURE0);
			// Bind the texture to the active unit for drawing
			//Assign different shard textures to every 5th shard
			switch (i % 5) {
			case 0:
				shard1->bind();
				quadShader->set_uniform("tex", 0);
				quad->set_attributes(*quadShader);
				quad->draw();
				shard1->unbind();
			case 1:
				shard2->bind();
				quadShader->set_uniform("tex", 0);
				quad->set_attributes(*quadShader);
				quad->draw();
				shard2->unbind();
			case 2:
				shard3->bind();
				quadShader->set_uniform("tex", 0);
				quad->set_attributes(*quadShader);
				quad->draw();
				shard3->unbind();
			case 3:
				shard4->bind();
				quadShader->set_uniform("tex", 0);
				quad->set_attributes(*quadShader);
				quad->draw();
				shard4->unbind();
			case 4:
				shard5->bind();
				quadShader->set_uniform("tex", 0);
				quad->set_attributes(*quadShader);
				quad->draw();
				shard5->unbind();
			}
			quadShader->unbind();
		}
	}
	glDisable(GL_BLEND);
}
