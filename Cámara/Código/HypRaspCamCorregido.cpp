// [21 de Noviembre] Inicia la preparación de la distribución en SoftwareX (Versión Corregida)

#include <iostream>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <string>
#include <math.h>
#include <fstream>
#include <sys/stat.h>
#include <sstream>
#include <streambuf>
#include <ctime>
#include <sys/wait.h>
#include "lstStructs.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define NUM_THREADS     5
#define     _OK         1
#define     _ERROR      0
#define     _FAIL       2

using namespace std;

void *sender(void *arg);
void error(const char *msg);
void obtainIP(char* host);
bool sendBigFrame( int newsockfd, const std::string &bigFrame );
void funcMotorDoAWalk( strReqImg *reqImg );
void recordVideo( strReqImg *reqImg );
void funcPrintFirst(int n, int max, char *buffer);
bool getRaspImg(strReqImg *reqImg, const std::string& fileName);
std::string file2String( const std::string &fileName );
bool funcSaveFile(const char* fileName, const char* data, int fileLen, int w, int h);
std::string genSLIDECommand(strReqImg *reqImg);
std::string genRaspiVideoCommand(strReqImg *reqImg);
std::string genCommand(strReqImg *reqImg, const std::string& fileName);
bool reqImgIsValid( strReqImg *reqImg );
int fileExists( const std::string& fileName );
int applyTimeLapseUsingRaspistill(strReqImg *reqImg);
bool funcPathExists( const std::string &fileName );
bool funcCreateFolder( const std::string &folderName );
bool funcDeleteFolderRecursively( const std::string &folderName );
bool funcClearFolder( const std::string &folderName );
void startTimLapse(int newsockfd, strReqImg *reqImg);
int startGenerateSlideCube(int newsockfd, strReqImg *reqImg);
uint8_t* subimage( int x1, int y1, int croppedRows, int croppedCols, uint8_t* img, int imgRows, int imgCols );
int funcGenericSendFrame( int newsockfd, u_int8_t* frame, int lenToSend );
int funcSendOneMessage( int newsockfd, void* frame, int lenToSend );
int funcSendERROR( int newsockfd, u_int8_t ID );
int funcSendACK( int newsockfd, u_int8_t ID );
void funcMessage( const std::string &msg );
int checkIfRequestedFileExists( int sockfd, strReqFileInfo* reqFileInfo );
int sendRequestedFile( int sockfd, strReqFileInfo* reqFileInfo );
int deleteFileIfExists( const char* fileName );
int readFileContain( const string &fileName, string* contain );

unsigned int PORT;
std::string SERIAL_PORT;

int main(int argc, char *argv[])
{
	int aux;
	
	if( argc < 2 || argc > 3 )
	{	
		printf("Usage 1: ./HypRaspCam <Port>\n");
		printf("Usage 2: ./HypRaspCam <Port> </dev/ttyUSBX>\n");
		printf("Example: ./HypRaspCam 51717 /dev/ttyUSB0\n");
		fflush(stdout);
		return -1;
	}	
	
	std::string tmpPort = argv[1];
	std::istringstream(tmpPort) >> PORT;	
	
	if( argc == 3 )
	{
		SERIAL_PORT = argv[2];
	}

	// Uso consistente de asignación de memoria con malloc/free (o se pudo usar new/delete)
	strReqImg *reqImg 			= (strReqImg*)malloc(sizeof(strReqImg));
	strReqFileInfo* reqFileInfo = (strReqFileInfo*)malloc(sizeof(strReqFileInfo));

	char* host = (char*)malloc(NI_MAXHOST * sizeof(char));
	obtainIP(host);

	string camName;
	if( readFileContain( "camName.HypRaspCam", &camName ) != _OK )
	{
		cout << "[ERROR] camName.HypRaspCam corrupt or not exists..." << endl;
		free(reqImg);
		free(reqFileInfo);
		free(host);
		return _ERROR;
	}
	if( camName.size() == 0 )
	{
		camName.clear();
		camName.append( "camNameDefault" );
	}
	FILE* pipe;
	std::string result;

	char bufferComm[frameBodyLen];
	frameStruct *frameReceived 			= (frameStruct*)malloc(sizeof(frameStruct));
	structRaspistillCommand* raspistillCommand = (structRaspistillCommand*)malloc(sizeof(structRaspistillCommand));
	structCamSelected *camSelected		= (structCamSelected*)malloc(sizeof(structCamSelected));
	
	unsigned int headerLen = sizeof(frameHeader);
	camSelected->On = false;
	int sockfd, newsockfd;
	socklen_t clilen;
	char buffer[frameBodyLen];

	struct sockaddr_in serv_addr, cli_addr;
	int n;
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0){
		error("ERROR opening socket");
	}
	
	int opt = 1;
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	bzero((char *) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(PORT);
	
	if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
	{
		error("ERROR on binding");
	}
  
	if( argc == 3 )
	{
		printf("Initializing Serial Port\n");
		std::string tmpCommand;

		tmpCommand = "sudo chmod o+rw " + SERIAL_PORT;
		pipe = popen(tmpCommand.c_str(), "r");
		if(pipe) pclose(pipe);

		tmpCommand = "stty -F " + SERIAL_PORT + " speed 9600 cs8 -cstopb -parenb";
		pipe = popen(tmpCommand.c_str(), "r");
		if(pipe) pclose(pipe);

		tmpCommand = "tail -f " + SERIAL_PORT + " &";
		pipe = popen(tmpCommand.c_str(), "r");
		if(pipe) pclose(pipe);

		fflush(stdout);
	}
  
	printf("Welcome!!!\n\n\n");
	fflush(stdout);
  
	unsigned int holdon = 1;
	while( holdon ){
		listen(sockfd, 5);
		clilen = sizeof(cli_addr);
		newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
		if (newsockfd < 0)
		{
			error("ERROR on accept");
		}
		bzero(buffer, frameBodyLen);
		n = read(newsockfd, buffer, frameBodyLen-1);
		if (n < 0)
		{
			error("ERROR reading from socket");
			close(newsockfd);
			continue;
		}

		aux = n - headerLen;       
		memcpy(frameReceived, buffer, n);

		printf("\nidMessage(%i) n(%i)\n", frameReceived->header.idMsg, n);
		
		switch( frameReceived->header.idMsg ){
			case 1: {
				printf("Hand-shaking\n");
				camSettings tmpCamSettings;
				tmpCamSettings.idMsg = (char)1;
				memcpy(&tmpCamSettings.IP, host, 15);        
				memcpy(&tmpCamSettings.Alias, camName.c_str(), 20);
				n = write(newsockfd, &tmpCamSettings, sizeof(camSettings));
				if (n < 0){
					error("ERROR writing to socket");
				}
				break;
			}
			case 2: {
				printf("Applying command -> %s\n", frameReceived->msg);
				result = "";
				pipe = popen(frameReceived->msg, "r");
				if(pipe) {
					try {
						while (!feof(pipe)) {
							if (fgets(bufferComm, frameBodyLen, pipe) != NULL){
								result.append(bufferComm);
							}
						}
					} catch (...) {
						pclose(pipe);
						throw;
					}
					pclose(pipe);
				}

				result = (result.size() > 1) ? result : " ";
				if( sendBigFrame(newsockfd, result) ){
					printf("Command result sent\n");
				}else{
					printf("Command result NOT sent\n");
				}
				break;
			}
			case 3: {
				printf("Applying command -> %s\n", frameReceived->msg);
				pipe = popen(frameReceived->msg, "r");
				if(pipe) pclose(pipe);
				result.assign("done");

				printf("\n<%s>\n", result.c_str());
				if( sendBigFrame(newsockfd, result) ){
					printf("Command DONE sent\n");
				}else{
					printf("Command DONE NOT sent\n");
				}
				break;
			}
			case 4: {
				memset(raspistillCommand, '\0', sizeof(structRaspistillCommand));        
				memcpy(raspistillCommand, frameReceived, sizeof(structRaspistillCommand));
		
				deleteFileIfExists(raspistillCommand->fileName);
						
				printf("Raspistill -> %s\n", raspistillCommand->raspiCommand);
				
				pipe = popen(raspistillCommand->raspiCommand, "r");
				if(pipe) pclose(pipe);
				printf("Command executed\n");
				
				if( fileExists(raspistillCommand->fileName) )
					buffer[1] = 1;
				else
					buffer[1] = 0;
		  
				write(newsockfd, &buffer, 2);
				break;
			}
			case 5: {
				buffer[1] = 1;	
				write(newsockfd, &buffer, 2);
				
				if( frameReceived->header.trigeredTime > 0 )
				{
					printf("Delaying time (%d seconds)\n", frameReceived->header.trigeredTime);
					fflush(stdout);
					sleep(frameReceived->header.trigeredTime);
				}
				
				printf("Applying command -> %s\n", frameReceived->msg);

				result = "";
				pipe = popen(frameReceived->msg, "r");
				if(pipe) {
					try {
						while (!feof(pipe)) {
							if (fgets(bufferComm, frameBodyLen, pipe) != NULL){
								result.append(bufferComm);
							}
						}
					} catch (...) {
						pclose(pipe);
						throw;
					}
					pclose(pipe);
				}
				printf("Command Executed\n");
				break;
			}
			case 6:
				break;
			case 7: {
				memset(reqImg, '\0', sizeof(strReqImg));        
				memcpy(reqImg, frameReceived, sizeof(strReqImg));
				memset(buffer, '\0', 3);
				
				if( reqImgIsValid(reqImg) )
					buffer[1] = 1;
				else
					buffer[1] = 0;	
				write(newsockfd, &buffer, 2);
				
				if( buffer[1] != 0 ){
					printf("Making the snapshot by applying raspistill\n");
					std::string fileName = "./tmpSnapshots/tmpImg.RGB888";
					if( getRaspImg(reqImg, fileName) ){				
						printf("Snapshot [OK]\n");
					}else{
						printf("Snapshot [Fail]\n");
					}
				}
				break;
			}
			case 8:
				break;
			case 9: {
				memset(reqImg, '\0', sizeof(strReqImg));        
				memcpy(reqImg, frameReceived, sizeof(strReqImg));
				
				if( reqImgIsValid(reqImg) )
				{
					buffer[1] = 1;
					write(newsockfd, &buffer, 2);
					startTimLapse(newsockfd, reqImg);
				}
				else
				{
					buffer[1] = 0;
					write(newsockfd, &buffer, 2);
				}
				break;
			}
			case 10: {
				// CORREGIDO: Uso estricto de sizeof(strReqFileInfo) en lugar de sizeof(puntero)
				memset(reqFileInfo, '\0', sizeof(strReqFileInfo));        
				memcpy(reqFileInfo, frameReceived, (n < (int)sizeof(strReqFileInfo)) ? n : sizeof(strReqFileInfo));
				checkIfRequestedFileExists(newsockfd, reqFileInfo);
				break;
			}
			case 11: {
				// CORREGIDO: Uso estricto de sizeof(strReqFileInfo)
				memset(reqFileInfo, '\0', sizeof(strReqFileInfo));        
				memcpy(reqFileInfo, frameReceived, (n < (int)sizeof(strReqFileInfo)) ? n : sizeof(strReqFileInfo));
				sendRequestedFile(newsockfd, reqFileInfo);
				break;
			}
			case 12: {
				memset(reqImg, '\0', sizeof(strReqImg));        
				memcpy(reqImg, frameReceived, sizeof(strReqImg));
				buffer[1] = 1;
				write(newsockfd, &buffer, 2);
				funcClearFolder("tmpSnapVideos");
				
				pid_t pid;
				if( argc == 3 )
				{
					if ( (pid = fork()) == 0 )
					{
						// Proceso hijo: cerramos sockets innecesarios heredados
						close(sockfd);
						close(newsockfd);
						funcMotorDoAWalk(reqImg);
						free(reqImg);
						free(reqFileInfo);
						free(host);
						free(frameReceived);
						free(raspistillCommand);
						free(camSelected);
						exit(0);
					}
					else if(pid > 0)
					{
						recordVideo(reqImg);
						// Evitar procesos zombis recolectando al hijo en segundo plano de manera no bloqueante o dejando que el SO gestione
						waitpid(pid, NULL, WNOHANG);
					}
					else
					{
						recordVideo(reqImg);
					}
				}
				else
				{
					recordVideo(reqImg);
				}
				break;
			}
			default:
				n = write(newsockfd, "Default", 8);
				if (n < 0)
				{
					error("ERROR writing to socket");
				}
				holdon = 0;
				break;
		}
		close(newsockfd);
	}
  
	free(reqImg);
	free(reqFileInfo);
	free(host);
	free(frameReceived);
	free(raspistillCommand);
	free(camSelected);

	close(sockfd);
	printf("It finishes...\n");
	return 0;
}

void funcMotorDoAWalk( strReqImg *reqImg )
{
	int16_t timeMs, degreeEnd, degreeInit;
	degreeInit = reqImg->motorWalk.degreeIni;
	timeMs = reqImg->motorWalk.durationMs;
	degreeEnd = reqImg->motorWalk.degreeEnd;
	
	FILE* pipe;	
	int i;
	std::string tmpCommand;
	
	tmpCommand = "echo 2 > " + SERIAL_PORT;
	for(i=0; i<360; i++)
	{		
		pipe = popen(tmpCommand.c_str(), "r");
		if(pipe) pclose(pipe);
	}
	
	tmpCommand = "echo 1 > " + SERIAL_PORT;
	for(i=0; i<degreeInit; i++)
	{
		pipe = popen(tmpCommand.c_str(), "r");
		if(pipe) pclose(pipe);
		usleep(10*1000);
	}
	fflush(stdout);
	
	usleep(reqImg->motorWalk.stabilizingMs*1000);
	
	tmpCommand = "echo 1 > " + SERIAL_PORT;
	int delayMs = ceil( (float)timeMs / (float)(degreeEnd - degreeInit + 1) );
	for(i=degreeInit; i<=degreeEnd; i++)
	{
		pipe = popen(tmpCommand.c_str(), "r");
		if(pipe) pclose(pipe);
		usleep(delayMs*1000);
	}
	fflush(stdout);
	
	usleep(2000*1000);
	
	tmpCommand = "echo 2 > " + SERIAL_PORT;
	for(i=1; i<=360; i++)
	{		
		pipe = popen(tmpCommand.c_str(), "r");
		if(pipe) pclose(pipe);
	}
	fflush(stdout);
}

void recordVideo( strReqImg *reqImg )
{
	// CORREGIDO: Se destruye correctamente el objeto string devuelto para evitar fuga de memoria
	std::string raspiVIDEOCommand = genRaspiVideoCommand(reqImg);
	printf("RaspiVIDEOCommand: %s\n", raspiVIDEOCommand.c_str());
	
	FILE* pipe = popen(raspiVIDEOCommand.c_str(), "r");
	if(pipe) pclose(pipe);
}

int checkIfRequestedFileExists( int sockfd, strReqFileInfo* reqFileInfo )
{	
	std::string internFileName;
	internFileName.assign(reqFileInfo->fileName);
	
	printf("Client asked for: %s\n", internFileName.c_str());
	fflush(stdout);
	
	if( fileExists(internFileName) )
	{
		funcMessage("File exists");
		funcSendACK(sockfd, 101);
	}
	else
	{
		funcMessage("File DOES NOT exists");
		funcSendERROR(sockfd, 101);
	}
	
	return -1;
}

int sendRequestedFile( int sockfd, strReqFileInfo* reqFileInfo )
{
	int n;
	std::string tmpFile = file2String(reqFileInfo->fileName);
	
	int fileLen = tmpFile.size();
	printf("fileLen: %d\n", fileLen);
	fflush(stdout);
	
	n = write(sockfd, &fileLen, sizeof(int));
	if(n < 0) return -1;
	
	n = write(sockfd, tmpFile.c_str(), fileLen);
	printf("Bytes inyectados: %d\n", n);

	return n;
}

int applyTimeLapseUsingRaspistill(strReqImg *reqImg)
{
	funcClearFolder("tmpTimeLapse");
	
	std::string timeLapseCommand = genSLIDECommand(reqImg);
	printf("Comm: %s\n", timeLapseCommand.c_str());
			
	FILE* pipe = popen(timeLapseCommand.c_str(), "r");
	if(pipe) pclose(pipe);
	
	int i, numImgs, expectedNumImgs;
	std::ostringstream intToStr; 
	std::string tmpFileName;
	numImgs = 0;
	expectedNumImgs = ceil(
							(float)(reqImg->slide.degreeEnd - reqImg->slide.degreeIni) /
							(float)reqImg->slide.degreeJump
						   );
	for( i=1; i<=expectedNumImgs; i++ )
	{
		intToStr.str("");
		intToStr << i;		
		tmpFileName = "./tmpTimeLapse/" + intToStr.str() + ".png";
		if( funcPathExists(tmpFileName) == true )
		{
			numImgs++;
		}
	}
	printf("Acquired imagery: %d of %d\n", numImgs, expectedNumImgs);
	return numImgs;
}

std::string file2String( const std::string &fileName )
{
	std::ifstream t(fileName.c_str());
	if(!t.is_open()) return "";
	std::string str;
	t.seekg(0, std::ios::end);
	str.reserve(t.tellg());
	t.seekg(0, std::ios::beg);
	str.assign(
					(std::istreambuf_iterator<char>(t)),
					std::istreambuf_iterator<char>()
			  );
	return str;
}

bool funcSaveFile(const char* fileName, const char* data, int fileLen, int w, int h)
{
	std::ofstream outFile(fileName, std::ios::binary);
	if(!outFile.is_open()) return false;
	outFile << "P6\n" << w << " " << h << " 255\n";
	outFile.write(data, fileLen);
	return true;
}

bool getRaspImg(strReqImg *reqImg, const std::string &fileName){
	std::string raspistillCommand = genCommand(reqImg, fileName);
	printf("Comm: %s\n", raspistillCommand.c_str());
	
	FILE* pipe = popen(raspistillCommand.c_str(), "r");
	if(pipe) pclose(pipe);
	
	if(!fileExists(fileName)){
		return false;
	}
	
	if( reqImg->needCut )
	{
		float x, y, w, h;	
		if(reqImg->squApert)
		{
			x = reqImg->sqApSett.rectX;
			y = reqImg->sqApSett.rectY;
			w = reqImg->sqApSett.rectW;
			h = reqImg->sqApSett.rectH;
		}
		else
		{
			x = reqImg->diffArea.rectX;
			y = reqImg->diffArea.rectY;
			w = reqImg->diffArea.rectW;
			h = reqImg->diffArea.rectH;
		}
		
		int imgCols, imgRows, bpp;
		uint8_t* tmpImg = stbi_load(_PATH_IMG_GEN_USING_RASP, &imgCols, &imgRows, &bpp, 3);	
		if(tmpImg) {
			uint8_t* croppedSlide = subimage(x, y, h, w, tmpImg, imgRows, imgCols);
			if( saveBinFile_From_u_int8_T(_PATH_IMG_GEN_USING_RASP, croppedSlide, (w*h*3)) )
				std::cout << "File cropped successfully" << std::endl;
			else
				std::cout << "ERROR cropping image" << std::endl;
			stbi_image_free(tmpImg);
			free(croppedSlide);
		}
	}
	return true;	
}

bool reqImgIsValid( strReqImg *reqImg )
{
	if( reqImg->imgCols <= 0 )
	{
		printf("Width <= 0\n");
		return false;
	}	
	if( reqImg->imgRows <= 0 )
	{
		printf("Height <= 0\n");
		return false;
	}	
	
	if( reqImg->needCut == true )
	{
		printf("Cut required\n");
		if( reqImg->sqApSett.rectW <= 0 ) { printf("ROI W <= 0\n"); return false; }
		if( reqImg->sqApSett.rectH <= 0 ) { printf("ROI H <= 0\n"); return false; }
	}
	if( reqImg->isSlide == true )
	{
		if( reqImg->slide.x1 < 0 ) { printf("x1 < 0\n"); return false; }
		if( reqImg->slide.y1 < 0 ) { printf("y1 < 0\n"); return false; }
		if( reqImg->slide.rows1 < 1 ) { printf("rows1 < 1\n"); return false; }
		if( reqImg->slide.cols1 < 1 ) { printf("cols1 < 1\n"); return false; }
		if( reqImg->slide.speed < 100 ) { printf("speed < 100\n"); return false; }
		if( reqImg->slide.degreeEnd < 0 || reqImg->slide.degreeEnd > 360 ) { printf("degreeEnd invalid\n"); return false; }
	}
	return true;
}

std::string genSLIDECommand(strReqImg *reqImg)
{
	std::string tmpCommand = "raspistill -o ./tmpTimeLapse/%d.png -n -q 100 -gc";
	std::ostringstream numberToString;
	
	int duration = ceil( (float)(reqImg->slide.degreeEnd - reqImg->slide.degreeIni) / 
					   (float)reqImg->slide.degreeJump ) * reqImg->slide.speed;
	numberToString << " -t " << duration << " -tl " << reqImg->slide.speed;
	tmpCommand.append(numberToString.str());
	
	numberToString.str("");
	numberToString << reqImg->imgCols;
	tmpCommand.append(" -w " + numberToString.str());
	
	numberToString.str("");
	numberToString << reqImg->imgRows;
	tmpCommand.append(" -h " + numberToString.str());

	if(reqImg->raspSett.ColorBalance)
	{
		tmpCommand.append(" -ifx colourbalance");
	}
	if(reqImg->raspSett.Denoise)
	{
		tmpCommand.append(" -ifx denoise");
	}	
	
	int shutSpeed = reqImg->raspSett.ShutterSpeed;
	if( (!reqImg->squApert && shutSpeed > 0) || (reqImg->fullFrame && shutSpeed > 0) )
	{
		numberToString.str("");
		numberToString << shutSpeed;
		tmpCommand.append(" -ss " + numberToString.str());
	}
	
	if(strcmp((char*)reqImg->raspSett.AWB, "none") != 0)
	{
		std::string sAWB((char*)reqImg->raspSett.AWB, sizeof(reqImg->raspSett.AWB));
		tmpCommand.append(" -awb " + sAWB);
	}
	
	if(strcmp((char*)reqImg->raspSett.Exposure, "none") != 0)
	{
		std::string sExposure((char*)reqImg->raspSett.Exposure, sizeof(reqImg->raspSett.Exposure));
		tmpCommand.append(" -ex " + sExposure);
	}
	
	if( reqImg->raspSett.ISO > 0 )
	{
		numberToString.str("");
		numberToString << reqImg->raspSett.ISO;
		tmpCommand.append(" -ISO " + numberToString.str());
	}
	return tmpCommand;
}

std::string genRaspiVideoCommand(strReqImg *reqImg)
{
	std::string tmpCommand = "raspivid -o ";       
	tmpCommand.append(reqImg->video.o);       
	std::ostringstream auxIntToString;
	
	if( reqImg->video.t > 0 )
	{
		auxIntToString << (1000 * reqImg->video.t);
		tmpCommand.append(" -t " + auxIntToString.str());
	}
	else
	{
		tmpCommand.append(" -t 3000");
	}
	
	if( reqImg->video.w > 0 )
	{
		auxIntToString.str("");
		auxIntToString << reqImg->video.w;
		tmpCommand.append(" -w " + auxIntToString.str());        
	}
	
	if( reqImg->video.h > 0 )
	{
		auxIntToString.str("");
		auxIntToString << reqImg->video.h;
		tmpCommand.append(" -h " + auxIntToString.str());        
	}

	if( reqImg->video.fps > 0 )
	{
		auxIntToString.str("");
		auxIntToString << reqImg->video.fps;
		tmpCommand.append(" -fps " + auxIntToString.str());        
	}
	
	if( reqImg->video.b > 0 )
	{
		auxIntToString.str("");
		auxIntToString << reqImg->video.b;
		tmpCommand.append(" -b " + auxIntToString.str());	
	}
  
	if( reqImg->video.v > 0 ) tmpCommand.append(" -v ");
	if( reqImg->video.d > 0 ) tmpCommand.append(" -d ");
	if( reqImg->video.e > 0 ) tmpCommand.append(" -e ");
	if( reqImg->video.set > 0 ) tmpCommand.append(" -set ");
		
	if( reqImg->video.md > 0 )
	{
		auxIntToString.str("");
		auxIntToString << reqImg->video.md;
		tmpCommand.append(" --mode " + auxIntToString.str());        
	}
	
	if( reqImg->video._if > 0 ) tmpCommand.append(" -if ");
	if( reqImg->video.fl > 0 ) tmpCommand.append(" -fl ");
	if( reqImg->video.pts > 0 ) tmpCommand.append(" -pts ");
		
	if( strcmp(reqImg->video.cd, "MJPEG") == 0 )
		tmpCommand.append(" -cd MJPEG ");
		
	if( reqImg->video.sh != '\0' )
	{
		auxIntToString.str("");
		auxIntToString << reqImg->video.sh;
		tmpCommand.append(" -sh " + auxIntToString.str());        
	}
	
	if( reqImg->video.co != '\0' )
	{
		auxIntToString.str("");
		auxIntToString << reqImg->video.co;
		tmpCommand.append(" -co " + auxIntToString.str());
	}
	
	if( reqImg->video.br != '\0' )
	{
		auxIntToString.str("");
		auxIntToString << reqImg->video.br;
		tmpCommand.append(" -br " + auxIntToString.str());
	}
	
	if( reqImg->video.sa != '\0' )
	{
		auxIntToString.str("");
		auxIntToString << reqImg->video.sa;
		tmpCommand.append(" -sa " + auxIntToString.str());
	}
	
	if( reqImg->video.ISO != '\0' )
	{
		auxIntToString.str("");
		auxIntToString << reqImg->video.ISO;
		tmpCommand.append(" -ISO " + auxIntToString.str());
	}
	
	if( reqImg->video.vs > 0 ) tmpCommand.append(" -vs ");        

	return tmpCommand;	
}

std::string genCommand(strReqImg *reqImg, const std::string& fileName)
{
	std::string tmpCommand = "raspistill -o " + fileName + " -n -q 100 -gc";
	std::ostringstream ss;
	
	if(reqImg->raspSett.ColorBalance)
	{
		tmpCommand.append(" -ifx colourbalance");
	}
	if(reqImg->raspSett.Denoise)
	{
		tmpCommand.append(" -ifx denoise");
	}
	
	int shutSpeed = reqImg->raspSett.SquareShutterSpeed;
	if( (reqImg->squApert && shutSpeed > 0))
	{		
		ss.str("");
		ss << shutSpeed;
		tmpCommand.append(" -ss " + ss.str());
	}
	
	shutSpeed = reqImg->raspSett.ShutterSpeed;
	if( (!reqImg->squApert && shutSpeed > 0) || (reqImg->fullFrame && shutSpeed > 0) )
	{
		ss.str("");
		ss << shutSpeed;
		tmpCommand.append(" -ss " + ss.str());
	}
	
	if( reqImg->raspSett.TriggerTime > 0 )
	{
		ss.str("");
		ss << (reqImg->raspSett.TriggerTime * 1000);
		tmpCommand.append(" -t " + ss.str());
	}
	else
	{
		tmpCommand.append(" -t 250");
	}	
	
	ss.str("");
	ss << reqImg->imgCols;
	tmpCommand.append(" -w " + ss.str());
	
	ss.str("");
	ss << reqImg->imgRows;
	tmpCommand.append(" -h " + ss.str());
	
	if(strcmp((char*)reqImg->raspSett.AWB, "none") != 0)
	{
		std::string sAWB((char*)reqImg->raspSett.AWB, sizeof(reqImg->raspSett.AWB));
		tmpCommand.append(" -awb " + sAWB);
	}
	
	if(strcmp((char*)reqImg->raspSett.Exposure, "none") != 0)
	{
		std::string sExposure((char*)reqImg->raspSett.Exposure, sizeof(reqImg->raspSett.Exposure));
		tmpCommand.append(" -ex " + sExposure);
	}
	
	if( reqImg->raspSett.ISO > 0 )
	{
		ss.str("");
		ss << reqImg->raspSett.ISO;
		tmpCommand.append(" -ISO " + ss.str());
	}
	return tmpCommand;
}

int fileExists( const std::string& fileName )
{
	struct stat buffer;
	return( stat(fileName.c_str(), &buffer) == 0 );
}

bool sendBigFrame( int newsockfd, const std::string &bigFrame )
{
	unsigned int fileLen = bigFrame.size();
	int i, n;
	char buffer[frameBodyLen];
	
	if( fileLen < 1 )
	{
		std::string result = "Error reading file";
		frameStruct frame2Send;
		frame2Send.header.idMsg = (char)3;  
		frame2Send.header.consecutive = 1;
		frame2Send.header.bodyLen = result.size();
		bzero(frame2Send.msg, frameBodyLen);        
		std::copy(result.begin(), result.end(), frame2Send.msg);
		unsigned int tmpFrameLen = sizeof(frameHeader) + frame2Send.header.bodyLen;
		n = write(newsockfd, &frame2Send, tmpFrameLen);        
		if (n < 0) error("ERROR writing to socket");
	}
	else
	{
		n = write(newsockfd, &fileLen, sizeof(unsigned int));
		if (n < 0) error("ERROR writing to socket");

		n = read(newsockfd, buffer, frameBodyLen-1);
		if (n < 0) error("ERROR reading from socket");
		
		int aux = ceil( (float)fileLen / (float)frameBodyLen );
		for(i=1; i<=aux; i++)
		{
			bzero(buffer, frameBodyLen);
			int chunkLen = ((i-1)*frameBodyLen + frameBodyLen < fileLen) ? frameBodyLen : (fileLen - (i-1)*frameBodyLen);
			memcpy(buffer, &bigFrame[(i-1)*frameBodyLen], chunkLen);
			n = write(newsockfd, buffer, frameBodyLen);
			if (n < 0) error("ERROR writing to socket");
			
			n = read(newsockfd, buffer, frameBodyLen-1);
			if (n < 0) error("ERROR reading from socket");
		}
	}
	return true;
}

void obtainIP(char* host)
{
	FILE *f = fopen("/proc/net/route", "r");
	if(!f) return;
	char line[100], *p, *c;
	char defaultInterface[20] = "";
	
	while(fgets(line, 100, f))
	{
		p = strtok(line, " \t");
		c = strtok(NULL, " \t");
		if(p != NULL && c != NULL)
		{
			if(strcmp(c, "00000000") == 0)
			{
				strncpy(defaultInterface, p, sizeof(defaultInterface)-1);
				break;
			}
		}
	}
	fclose(f);
	
	int fm = AF_INET;
	struct ifaddrs *ifaddr, *ifa;
	if (getifaddrs(&ifaddr) == -1) return;
	
	for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) 
	{
		if (ifa->ifa_addr == NULL) continue;
		int family = ifa->ifa_addr->sa_family;
		
		if(strcmp(ifa->ifa_name, defaultInterface) == 0)
		{
			if (family == fm) 
			{
				int s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
				if (s == 0) break;
			}
		}
	}
	freeifaddrs(ifaddr);
}

bool funcPathExists( const std::string &fileName )
{
	FILE* fileAsked = fopen(fileName.c_str(), "r");
	if( fileAsked )
	{
		fclose(fileAsked);
		return true;
	}	
	return false;
}

bool funcCreateFolder( const std::string &folderName )
{
	if( funcPathExists(folderName) == true )
	{
		return false;
	}
	else
	{
		std::string tmpCommand = "mkdir -m777 " + folderName;      
		system(tmpCommand.c_str());
	}
	return true;
}

bool funcDeleteFolderRecursively( const std::string &folderName )
{
	if( funcPathExists(folderName) == false )
	{
		return false;
	}
	else
	{
		std::string tmpCommand = "rm -r " + folderName;      
		system(tmpCommand.c_str());
	}
	return true;
}

bool funcClearFolder( const std::string &folderName )
{
	if( funcPathExists(folderName) == true )
		if( funcDeleteFolderRecursively(folderName) == false )
			return false;
	return funcCreateFolder(folderName);	
}

void startTimLapse(int newsockfd, strReqImg *reqImg)
{
	int n;
	strNumSlideImgs strNumImgs;
	char buffer[frameBodyLen];
	
	n = read(newsockfd, buffer, frameBodyLen-1);
	if (n < 0) error("Sock: Before start time lapse");

	strNumImgs.idMsg = buffer[0];
	strNumImgs.numImgs = applyTimeLapseUsingRaspistill(reqImg);
	n = sizeof(strNumSlideImgs);
	memcpy(buffer, &strNumImgs, n);
	write(newsockfd, &buffer, n+1);
	
	n = read(newsockfd, buffer, frameBodyLen-1);
	if (n < 0) error("Sock: Before start time lapse");
	
	if( buffer[0] == '1' )
	{
		std::cout << "Data-cube generation process started" << std::endl;
		startGenerateSlideCube(newsockfd, reqImg);
	}
	else
	{
		std::cout << "Time lapse process finished" << std::endl;
	}
}

int startGenerateSlideCube(int newsockfd, strReqImg *reqImg)
{
	int i, expectedN;
	std::string tmpImgName;
	std::ostringstream ss;
	expectedN = ceil(
						(float)(reqImg->slide.degreeEnd - reqImg->slide.degreeIni) /
						(float)reqImg->slide.degreeJump
					 );
	for( i=1; i<=expectedN; i++ )
	{
		ss.str("");
		ss << i;
		tmpImgName = "./tmpTimeLapse/" + ss.str() + ".png";
		
		if( funcPathExists(tmpImgName) == true )
		{
			std::cout << "Cropping and transmitting image " << i << "..." << std::endl;
			write(newsockfd, &i, sizeof(int));			
			cropAndTransmitSlide(tmpImgName, newsockfd, reqImg);
		}	
	}
	std::cout << "Notify that finished" << std::endl;
	i = -1;
	write(newsockfd, &i, sizeof(int));
	std::cout << "Slide-Cube sent" << std::endl;
	fflush(stdout);
	return 1;
}

bool cropAndTransmitSlide( const std::string &tmpImgName, int newsockfd, strReqImg *reqImg )
{
	int imgCols, imgRows, bpp;
	int x1, y1, rows1, cols1;
	int x2, y2, rows2, cols2;
	
	x1 = reqImg->slide.x1;		x2 = reqImg->slide.x2;
	y1 = reqImg->slide.y1;		y2 = reqImg->slide.y2;
	rows1 = reqImg->slide.rows1;	rows2 = reqImg->slide.rows2;
	cols1 = reqImg->slide.cols1;	cols2 = reqImg->slide.cols2;
	
	uint8_t* tmpImg = stbi_load(tmpImgName.c_str(), &imgCols, &imgRows, &bpp, 3);
	if(!tmpImg) return false;

	uint8_t* croppedSlide = subimage(x1, y1, rows1, cols1, tmpImg, imgRows, imgCols);
	uint8_t* croppedDiff = subimage(x2, y2, rows2, cols2, tmpImg, imgRows, imgCols);

	int slideLen = (rows1 * cols1 * 3);
	int diffLen = (rows2 * cols2 * 3);
	int totalLen = slideLen + diffLen;
	uint8_t* cropImgTrans = (uint8_t*)malloc(totalLen);
	
	if(cropImgTrans) {
		memcpy(&cropImgTrans[0], croppedSlide, slideLen);
		memcpy(&cropImgTrans[slideLen], croppedDiff, diffLen);
		funcSendOneMessage(newsockfd, cropImgTrans, totalLen);
		free(cropImgTrans);
	}
	
	free(croppedSlide);
	free(croppedDiff);
	stbi_image_free(tmpImg);
	
	return true;
}

uint8_t* subimage( int x1, int y1, int croppedRows, int croppedCols, uint8_t* img, int imgRows, int imgCols )
{ 
	uint8_t* croppedImg = (uint8_t*)malloc(croppedRows * croppedCols * 3);
	if(!croppedImg) return NULL;
	
	if( croppedRows < 1 || croppedCols < 1 || x1 > imgCols || x1 < 0 || y1 > imgRows || y1 < 0 )
	{
		return croppedImg;
	}

	int beforeX1 = (x1 > 0) ? x1 : 0;
	int beforeY1 = (y1 > 0) ? y1 - 1 : 0;
	int r;
	uint8_t *ptr, *cropPtr;
	
	cropPtr = &croppedImg[0];
	ptr = &img[0];
	ptr += ((beforeY1 * imgCols) + beforeX1) * 3;

	for( r=0; r<croppedRows; r++ )
	{
		memcpy(cropPtr, ptr, croppedCols * 3);
		ptr += imgCols * 3;
		cropPtr += croppedCols * 3;
	}
	
	return croppedImg;
}

int funcGenericSendFrame( int newsockfd, u_int8_t* frame, int lenToSend )
{
	funcSendOneMessage(newsockfd, frame, lenToSend);
	return 1;
}

void funcMessage( const std::string &msg )
{
	std::cout << msg << std::endl;
	fflush(stdout);
}

int funcSendOneMessage( int newsockfd, void* frame, int lenToSend )
{
	int n = write(newsockfd, frame, lenToSend);
	if( n > 0 ) return n;
	return 0;
}

int funcSendACK( int newsockfd, u_int8_t ID )
{
	u_int8_t buffer[2];
	buffer[0] = 1;
	buffer[1] = ID;
	
	// CORREGIDO: El tamaño enviado ahora coincide con el tamaño reservado (2 bytes)
	int n = write(newsockfd, &buffer[0], 2);
	if( n == 2 ) return 1;
	return 0;
}

int funcSendERROR( int newsockfd, u_int8_t ID )
{
	u_int8_t buffer[2];
	buffer[0] = 0;
	buffer[1] = ID;
	
	// CORREGIDO: Tamaño correcto de 2 bytes
	int n = write(newsockfd, &buffer[0], 2);
	if( n == 2 ) return 1;
	return 0;
}

int saveBinFile_From_u_int8_T( std::string fileName, uint8_t *data, size_t len)
{
	std::ofstream fp;
	fp.open(fileName.c_str(), std::ios::out | std::ios::binary);
	if(!fp.is_open()) return 0;
	fp.write((char*)data, len);
	fp.close();
	return 1;
}

int deleteFileIfExists( const char* fileName )
{
	return unlink(fileName);
}

int readFileContain( const string &fileName, string* contain )
{ 
	std::ifstream myfile(fileName.c_str());
	if( myfile.is_open() )
	{
		if( !getline(myfile, *contain) )
		{
			myfile.close();
			return _FAIL;
		}
		myfile.close();
	}
	else
	{
		return _ERROR;
	}
	return _OK;
}

void error(const char *msg)
{
	perror(msg);
	exit(1);
}
