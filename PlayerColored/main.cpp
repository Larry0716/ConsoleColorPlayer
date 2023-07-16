#define _CRT_SECURE_NO_WARNINGS
#include <opencv.hpp>
#include <windows.h>
#include <conio.h>
#include <math.h>

#include <iostream>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <thread>
#include <string>
using namespace cv;

using std::cerr;
using std::endl;
using std::string;
using std::cout;
using std::cin;
using std::thread;

#define MAX_COL 1024
#define MAX_ROW 2048
#define TOTAL_FRAME 4094
#define ESC "\x1b"
#define CSI "\x1b["
#define EPS 2

struct PIXEL {
	int R,G,B;
	PIXEL(int r, int g, int b):R(r),G(g),B(b){}
	PIXEL():R(-1),G(-1),B(-1){}
	bool operator ==(const PIXEL &oth) const {
		int dr = labs(R-oth.R);
		int dg = labs(G-oth.G);
		int db = labs(B-oth.B);
		return dr<EPS && dg<EPS && db<EPS;
	}
	bool operator !=(const PIXEL &oth) const {
		return !(*this == oth);
	}
};

struct Message{
	char *buffer;
	DWORD dwBufferSize;
	Message():buffer(NULL),dwBufferSize(0){}
	Message(char *buf, DWORD size): buffer(buf), dwBufferSize(size){}
};

template <typename T>
class MsgQueue{
	public:
		size_t size(){
			std::unique_lock<std::mutex> lg(mtx);
			return q.size();
		}
		void front(T &msg){
			std::unique_lock<std::mutex> lg(mtx);
			msg = q.front();
		}
		void pop(){
			std::unique_lock<std::mutex> lg(mtx);
			q.pop();
			cv.notify_one();
		}
		void push(const T &value){
			std::unique_lock<std::mutex> lg(mtx);
			q.push(value);
			cv.notify_one();
		}
		bool empty(){
			std::unique_lock<std::mutex> lg(mtx);
			return q.empty();
		}
		void waitSize(int target, const T &msg){
			std::unique_lock<std::mutex> lg(mtx);
			while(q.size() > target) cv.wait(lg);
			q.push(msg); 
		}
	private:
		std::mutex mtx;
		std::queue<T> q;
		std::condition_variable cv;
};

MsgQueue<Message> mq;

void SplitVideoToFrames(std::string filename) {
	VideoCapture capture(filename.c_str());
	if (!capture.isOpened()) {
		cerr << "Failed to open video file." << endl;
		return;
	}
	bool stop = false;
	Mat buffer;
	char strBuffer[1024];
	int cnt = 0;
	while(capture.read(buffer)){
		sprintf_s(strBuffer,1024,".\\frames\\%d.png",++cnt);
		imwrite(strBuffer,buffer);
		printf("Exported %d frames\r",cnt);
	}
}

void ConvertToText(int limit) {
	char srcnameBuf[1024];
	char dstnameBuf[1024];
	Mat srcImageBuf, imageBuf;
	for(int p=1;p<=limit;++p){
		printf("Now processing No.%d frame\r", p);
		sprintf_s(srcnameBuf, sizeof(srcnameBuf), "./frames/%d.png", p);
		sprintf_s(dstnameBuf, sizeof(dstnameBuf), "./txt/%d.tframe", p);
		srcImageBuf = imread(srcnameBuf);
		resize(srcImageBuf, imageBuf, Size(0, 0), 1.0 / 6, 1.0 / 10, INTER_AREA);
		string res;
		for (int i = 0,j; i < imageBuf.rows; i++) {
			PIXEL last;
			char stateBuf[128];
            for (j = 0; j < imageBuf.cols; j++) {
            	uchar *pix = imageBuf.ptr<uchar>(i,j);
            	PIXEL now(pix[2], pix[1], pix[0]);
            	if(last != now) {
            		last = now;
            		sprintf_s(stateBuf, sizeof(stateBuf), "%s48;2;%d;%d;%dm", CSI, now.R, now.G, now.B);
            		res += stateBuf;
				}
				res += " ";
			}
        	res += "\n";
        }
        res+=(CSI"0;0H");
        FILE *f1 = fopen(dstnameBuf, "w");
		if (f1 == nullptr) {
			cerr << "Failed to open tframe file" << endl;
			exit(-1);
		}
		fprintf(f1, "%s", res.c_str());
        fclose(f1);
	}
	printf("Finished!!                      \n");
}

DWORD SetFont(HANDLE hStdout){
	CONSOLE_FONT_INFOEX cfi;
    cfi.cbSize = sizeof(CONSOLE_FONT_INFOEX);
    COORD size;
    size.X = 3;
    size.Y = 5;
    cfi.dwFontSize = size;
    wcscpy(cfi.FaceName, L"Arial");
    cfi.FontWeight = 100;
    cfi.FontFamily = TMPF_TRUETYPE;
    cfi.nFont = 0;
    if(SetCurrentConsoleFontEx(hStdout, FALSE, &cfi)) {
    	return GetLastError();
	}
	return 0;
}

DWORD SetColorfulTerminal(HANDLE hStdout) {
    DWORD dwMode = 0;
    if (!GetConsoleMode(hStdout, &dwMode)) {
        return GetLastError();
    }
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (!SetConsoleMode(hStdout, dwMode)) {
        return GetLastError();
    }
    return 0;
}

void SetupTerminal(HANDLE hStdout){
	DWORD ret = SetFont(hStdout);
    if(ret) {
    	cerr<<"Failed to setup the terminal"<<endl;
    	exit(ret);
	}
    ret = SetColorfulTerminal(hStdout);
    if(ret){
    	cerr<<"Failed to setup the terminal"<<endl;
    	exit(ret);
	}
	printf(CSI"38;2;0;255;0mSuccessfully set up the terminal!\n");
}

void LoadFrame(int limit){
	char srcnameBuf[1024];
	DWORD dwFilesize = 0, dwFilesizeHigh = 0, dwRealRead;
	for(int i=1;i<=limit;i++){
		sprintf_s(srcnameBuf, sizeof(srcnameBuf), "./txt/%d.tframe", i);
		HANDLE hFile = CreateFileA(
			srcnameBuf,
			GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ,
			NULL,
			OPEN_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			NULL
		);
		dwFilesize = GetFileSize(hFile, &dwFilesizeHigh);
		dwFilesize |= dwFilesizeHigh;
		DWORD dwBufferSize = dwFilesize + 1;
		char *buffer = new char[dwBufferSize];
		ZeroMemory(buffer, dwBufferSize);
		BOOL ret = ReadFile(hFile, buffer, dwFilesize, &dwRealRead, NULL);
		if(!ret){
			cerr<<"Failed to LoadFrame: "<<srcnameBuf<<endl;
			exit(-1);
		}
		mq.waitSize(1000, Message(buffer, dwFilesize));
	}
}

int main() {
	int select;
	cout<<"1/2?: ";
	cin>>select;
	if(select!=2){
		string s;
		cin>>s;
		if(select == 1)
			SplitVideoToFrames(s);
		ConvertToText(TOTAL_FRAME);
	}
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    if(hStdout == INVALID_HANDLE_VALUE){
    	cerr<<"XXX ?????? @@ ?????XXX TxT."<<endl;
    	return GetLastError();
	}
    SetupTerminal(hStdout);
    int T = TOTAL_FRAME+1;
    thread tworker(LoadFrame, TOTAL_FRAME);
    Sleep(3000);
	printf("Ready.\n");
    _getch();
	system("cls");
    double c1 = clock();
    DWORD dwRealWrite = 0;
    while(--T){
    	while(!mq.size());
    	Message messBuf; 
    	mq.front(messBuf);
    	mq.pop();
    	WriteConsoleA(hStdout, messBuf.buffer, messBuf.dwBufferSize, &dwRealWrite, NULL);
    	delete[] messBuf.buffer;
	}
    double c2 = clock();
    printf("%.3lf", (c2 - c1) / CLOCKS_PER_SEC);
    system("pause");
    return 0;
}
