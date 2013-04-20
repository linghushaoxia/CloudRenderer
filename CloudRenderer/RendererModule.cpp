#include "stdafx.h"

#include "RendererModule.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

#include "ShaderManager.h"
#include "RenderUtility.h"
#include "Cube.h"

GLuint windowWidth = 800;
GLuint windowHeight = 600;
const char * windowTitle = "Real-timeish Cloud Renderer";

ShaderManager shaderManager;
GLuint billboardShaderProgram;
GLuint raycasterShaderProgram;
GLuint VAOs [2];
GLuint billboardVBO;
GLuint cubeVBO;

float nearPlane = 0.1f;
float farPlane = 25.0f;

float fieldOfView = 85.0f;
float tanFOV = tan( fieldOfView / 2.0f / 360 * 2 * 3.14f );

RendererModule::RendererModule() {
	showSplat = false;
	showVRC = true;
};

bool RendererModule::initialize( const int gridX, const int gridY, 
								 const int gridZ ) {

	// Initialize GLFW and opens a window
	if( glfwInit() != GL_TRUE ) {
		return false;
	}

	glfwOpenWindowHint( GLFW_OPENGL_VERSION_MAJOR, 3 );
	glfwOpenWindowHint( GLFW_OPENGL_VERSION_MINOR, 2 );
	glfwOpenWindowHint( GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE );
	glfwOpenWindowHint( GLFW_WINDOW_NO_RESIZE, GL_TRUE );

	if (glfwOpenWindow( windowWidth, windowHeight, 0, 0, 0, 0, 24, 8, 
		GLFW_WINDOW ) != GL_TRUE) {
			glfwTerminate();
			return false;
	}
	
	glfwSetWindowTitle( windowTitle );

	// Initialize GLEW
	glewExperimental = GL_TRUE;
	GLenum err = glewInit();
	if(err != GLEW_OK) {
		std::cerr << "Failed to initialize GLEW: " << 
			glewGetErrorString(err) << "\n";
		return false;
	}
	// Ignore the OpenGL error (1280) that glewInit() causes
	glGetError();

	std::cout << "Running OpenGL version " << glGetString(GL_VERSION) << "\n";

	glGenVertexArrays( 2, VAOs );
	glBindVertexArray( VAOs[0] );

	// Load and compile shaders
	billboardShaderProgram = shaderManager.createFromFile( 
		"BillboardShader.vert", "BillboardShader.frag" );
	raycasterShaderProgram = shaderManager.createFromFile( 
		"RaycasterShader.vert", "RaycasterShader.frag" );

	initializeTextures();

	// A single billboard data
	float vertexSize = 1.8f;
	float vertices[] = {
		//	Vertex position		      Texcoords
		-vertexSize,  vertexSize, 0.0f, 1.0f,  		// Vertex 1 (-X,  Y)
		-vertexSize, -vertexSize, 0.0f, 0.0f,		// Vertex 2 (-X, -Y)
		 vertexSize,  vertexSize, 1.0f, 1.0f,		// Vertex 3 ( X,  Y)
		 vertexSize,  vertexSize, 1.0f, 1.0f,		// Vertex 3 ( X,  Y)
		-vertexSize, -vertexSize, 0.0f, 0.0f,		// Vertex 2 (-X, -Y)
		 vertexSize, -vertexSize, 1.0f, 0.0f		// Vertex 4 ( X, -Y)
	};

	billboardVBO = createVBO( vertices, sizeof( vertices ) );
	defineBillboardLayout( billboardShaderProgram );

	// Create cube that encapsulates the grid for ray casting
	float cubeVertices[24];
	getCubeVertices( 0, 1, 0, 1, 0, 1, cubeVertices );

	cubeVBO = createVBO( cubeVertices, sizeof( cubeVertices )) ;
	glBindVertexArray( VAOs[1] );
	defineRaycasterLayout( raycasterShaderProgram );

	int cubeElements[36];
	getCubeElements( cubeElements );
	createEBO( cubeElements, sizeof( cubeElements ));

	// Initialize the camera and the projetion matrices
	camera.initialize( gridX, gridY, gridZ );
	perspectiveProjection = glm::perspective( 85.0f, 
		(float)windowWidth / (float)windowHeight, nearPlane, farPlane );
	orthographicProjection = glm::ortho( -(gridX/2.0f), gridX/2.0f, 
		-(gridY/2.0f), gridY/2.0f, 0.0f, 500.0f ); 

	// Set the sun position and sun position matrix (looking at the center of 
	// the cloud)
	sunPosition = glm::vec3( 300, 250, 250 ); 
	glm::vec3 lookAtPoint = glm::vec3( gridX/2, gridY/2, -gridZ/2 );
	sunTransformation = glm::lookAt( sunPosition, 
		lookAtPoint, glm::vec3(0, 0, 1));
	
	interpolatedData = new float ** [gridX];
	for( int i = 0; i != gridX; ++i ) {
		interpolatedData[i] = new float*[gridY];
		for (int j = 0; j != gridY; ++j ) 
			interpolatedData[i][j] = new float[gridZ];
	}


	return true;

}

void RendererModule::defineBillboardLayout( const GLuint billboardShaderProgram ) {

	// Define data layout
	GLint posAttrib = glGetAttribLocation( billboardShaderProgram, 
		"vertPos" );
	glEnableVertexAttribArray( posAttrib );
	glVertexAttribPointer( posAttrib, 2, GL_FLOAT, GL_FALSE, 
		4*sizeof(float), 0 );

	GLint texAttrib = glGetAttribLocation( billboardShaderProgram, 
		"texCoord" );
	glEnableVertexAttribArray( texAttrib );
	glVertexAttribPointer( texAttrib, 2, GL_FLOAT, GL_FALSE, 
		4*sizeof(float), (void*)( 2*sizeof(float) ) );

}

void RendererModule::defineRaycasterLayout( const GLuint raycasterShaderProgram ) {

	GLint posAttrib = glGetAttribLocation( raycasterShaderProgram, 
		"cubeVert" );
	glEnableVertexAttribArray( posAttrib );
	glVertexAttribPointer( posAttrib, 3, GL_FLOAT, GL_FALSE, 
		3*sizeof(float), 0 );

}

void RendererModule::draw( const SimulationData& data, GLFWmutex simMutex, 
						   const double time ) {
	
	// Update the camera
	camera.updateCamera();

	// Clear the screen with background (sky) color
	glClearColor( 155/256.0f, 225/256.0f, 251/256.0f, 1.0f );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	
	// Lock mutex because we will use data, which is shared with simulation
	glfwLockMutex( simMutex );

	interpolateCloudData( data, time );

	if( showVRC )
		renderRayCastingClouds( data, time );

	if( showSplat )
		renderSplattingClouds( data, time );

	glfwUnlockMutex( simMutex );

	// Check for errors
	GLint glErr = glGetError();
	if ( glErr ) std::cout << "OpenGL error " << glErr << "!\n";

	// Swap the buffer
	glfwSwapBuffers();

}

void RendererModule::interpolateCloudData( const SimulationData & data,  
										  const double time ) {

	int x = data.getGridLength();
	int y = data.getGridWidth();
	int z = data.getGridHeight();

	// Calculate relative difference for linear interpolation
	float relDiff = (time - data.nextTime)/(data.nextTime - data.prevTime);
	if( relDiff > 1.0f )relDiff = 1.0f;

	for( int i = 0; i < x; ++i ) 
		for( int j = 0; j < y; ++j ) 
			for( int k = 0; k < z; ++k )
				if( data.nextDen[i][j][k] > 0.0f ) {

					// Lineary interpolate the density
					interpolatedData[i][j][k] = data.prevDen[i][j][k] + relDiff
						* (data.nextDen[i][j][k] - data.prevDen[i][j][k] );

				}
				else
					interpolatedData[i][j][k] = 0.0f;
}

// Shade clouds by performing volume ray casting
void RendererModule::renderRayCastingClouds( const SimulationData & data, 
											const double time ) {

	glBindVertexArray( VAOs[1] );
	glUseProgram( raycasterShaderProgram );
	setUniform( "view", camera.getLookAtMatrix() );
	setUniform( "viewInverse", glm::inverse(camera.getLookAtMatrix()) );
	setUniform( "proj", perspectiveProjection );
	setUniform( "tanFOV", tanFOV );
	setUniform( "screenSize", glm::vec2( windowWidth, windowHeight ) );
	setUniform( "eyePosition", camera.getEyeLocation() );
	setUniform( "near", nearPlane );
	setUniform( "far", farPlane );

	glEnable( GL_CULL_FACE );
	glEnable( GL_DEPTH_TEST );

	int x = data.getGridLength();
	int y = data.getGridWidth();
	int z = data.getGridHeight();

	// Convert float*** to float* (stream of data)
	float* texData = new float[x*y*z];
	int pos = 0;
	for( int i = 0; i != x; ++i )
		for( int j = 0; j != y; ++j ) 
			for( int k = 0; k != z; ++k ) {
				texData[pos] = interpolatedData[i][j][k];
				++pos;
			}

	// Fill the data into 3D texture. A texture cell includes only one
	// component (GL_RED = density, float). 
	glTexImage3D( GL_TEXTURE_3D, 0, GL_R32F, x, y, z, 0, GL_RED, 
		GL_FLOAT, texData );

	delete[] texData;

	glDrawElements( GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0 );

} 

void RendererModule::renderSplattingClouds( const SimulationData & data,
										   const double time ) { 

	glBindVertexArray( VAOs[0] );
	glUseProgram( billboardShaderProgram );

	setUniform( "view", camera.getLookAtMatrix() );
	setUniform( "proj", perspectiveProjection );

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable( GL_BLEND );
	glDisable( GL_CULL_FACE );
	glDisable( GL_DEPTH_TEST );

	GLint uniPosition = glGetUniformLocation( billboardShaderProgram, "position" );
	GLint uniAlpha = glGetUniformLocation( billboardShaderProgram, "alpha" );

	int x = data.getGridLength();
	int y = data.getGridWidth();
	int z = data.getGridHeight();

	// Calculate relative difference for linear interpolation
	float relDiff = (time - data.nextTime)/(data.nextTime - data.prevTime);
	if( relDiff > 1.0f )relDiff = 1.0f;

	for( int i = 0; i < x; ++i ) 
		for( int j = 0; j < y; ++j ) 
			for( int k = 0; k < z; ++k )
				if( interpolatedData[i][j][k] > 0.0f) {
					// Build a translation (model) matrix in the shader, 
					// because it's a lot faster than creating the matrix 
					// here. 
					glUniform3f( uniPosition, i, j, -k );
					glUniform1f( uniAlpha, interpolatedData[i][j][k] );

					glDrawArrays( GL_TRIANGLES, 0, 6 );
				}
}

void RendererModule::terminate() {

	glDeleteProgram( billboardShaderProgram );
	shaderManager.terminate();
	glDeleteVertexArrays( 2, VAOs );
	deleteTextures();

	// Terminate GLFW
	glfwTerminate();

}