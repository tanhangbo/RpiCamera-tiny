#include <cv.h>
#include <highgui.h>
#include <pthread.h>
#include <iostream>
#include <thread>
#include <mutex>
#include <queue>
using namespace std;  



#define ERODE_TIMES 1
#define CAMERA_INDEX 0
/* seconds for record time */
#define VIDEO_COUNT 30
/* low FPS on slow Rpi machine */
#define VIDEO_FPS 5

queue<IplImage *> webcam_buf;


bool motion_detected = false;


IplImage *frame_gray1 = NULL;
IplImage *frame_gray2 = NULL;
IplImage *frame_gray3 = NULL;
IplImage *frame_result1 = NULL;
std::mutex frame_lock;


enum cur_state {
	INIT,
	NORMAL,
	DETECTED,
	RECORDING,
	END_REC,

};


bool detect_motion(IplImage *frame1, IplImage *frame2, IplImage *frame3)
{
	int black_pixels = 0;

	cvCvtColor(frame1, frame_gray1, CV_BGR2GRAY);
	cvCvtColor(frame2, frame_gray2, CV_BGR2GRAY);
	cvCvtColor(frame3, frame_gray3, CV_BGR2GRAY);

	cvAbsDiff(frame_gray1 ,frame_gray3, frame_gray1);// |gray1 - gray3| -> gray1
	cvAbsDiff(frame_gray2 ,frame_gray3, frame_gray2);// |gray2 - gray3| -> gray2

	cvAnd(frame_gray1, frame_gray2, frame_result1);
	/* draw the result, and can see the motion */
	cvThreshold(frame_result1, frame_result1, 35, 255, CV_THRESH_BINARY);

	cvErode(frame_result1, frame_result1, 0, ERODE_TIMES);
	black_pixels = cvCountNonZero(frame_result1);
	if (black_pixels > 500) {
		printf("MOTION_%d!\n", black_pixels);
		return true;
	}
	return false;
}


CvVideoWriter *create_video(CvVideoWriter **video_writer, IplImage *frame)
{
	char video_path[100] = {0};
	time_t current_time;
	struct tm * time_info;
	memset(video_path, 0, sizeof(video_path));
	time(&current_time);
	time_info = localtime(&current_time);
	strftime(video_path, 100, "./record/avi/%m%d_%H%M%S.avi", time_info);
	printf("creating video:%s\n", video_path);
	*video_writer =  cvCreateVideoWriter(video_path,
				CV_FOURCC('M', 'J', 'P', 'G'), VIDEO_FPS, cvGetSize(frame));

}



/*
	pop frame from queue,
	if fail, return null
*/
IplImage *pop_frame()
{
	IplImage *frame = NULL;
	frame_lock.lock();
	if(!webcam_buf.empty()) {
		frame = webcam_buf.front();
		webcam_buf.pop();
	}
	if (webcam_buf.size() > 100) {
		printf("your device is too slow, empty buffer!!\n");
		while (webcam_buf.size() > 0)
			webcam_buf.pop();
	}
	frame_lock.unlock();
	return frame;
}




void* dispatch_thread(void *arg)
{
	IplImage *frame = NULL;
	IplImage *frame1 = NULL;
	IplImage *frame2 = NULL;
	IplImage *frame3 = NULL;
	int init_step = 0;

	while (true) {
		frame = pop_frame();

		/* the init situation */
		if (NULL == frame) {
			sleep(1);
			continue;
		}


		/* we got here when frame is ready and resolution is got */
		if (frame1 == NULL) {
			frame1 =  cvCreateImage(cvGetSize(frame),frame->depth,3);
			frame2 =  cvCreateImage(cvGetSize(frame),frame->depth,3);
			frame3 =  cvCreateImage(cvGetSize(frame),frame->depth,3);
			frame_gray1 = cvCreateImage(cvGetSize(frame),frame->depth,1);
			frame_gray2 = cvCreateImage(cvGetSize(frame),frame->depth,1);
			frame_gray3 = cvCreateImage(cvGetSize(frame),frame->depth,1);
			frame_result1 = cvCreateImage(cvGetSize(frame),frame->depth,1);	
		}

		/* 
			copy for detect,release after copy
			cur_frame -> frame3 -> frame2 -> frame1 
		*/
		cvCopy(frame2, frame1);
		cvCopy(frame3, frame2);
		cvCopy(frame, frame3);
		cvReleaseImage(&frame);

		/* wait until copy done */
		if (init_step < 3) {
			init_step++;
			continue;
		} else {
			//printf("detecting!!!\n");
		}

		/* detect process */
		if(detect_motion(frame1, frame2, frame3)) {
			printf("detected!!!\n");
			/* need to copy again afterwards */
			init_step = 0;
			motion_detected = true;
		}

		/* sleep here to release CPU resource */
		while (motion_detected == true)
			sleep(1);


	}

}



void rhm_prepare_capture(CvCapture **capture)
{

	*capture = cvCreateCameraCapture(CAMERA_INDEX);
	assert(NULL != *capture);

	cvSetCaptureProperty(*capture, CV_CAP_PROP_FRAME_WIDTH, 640);
	cvSetCaptureProperty(*capture, CV_CAP_PROP_FRAME_HEIGHT, 480);

	return;
}


void rhm_start_threads()
{

	int err = 0;
	pthread_t tid[10];

	/* get buffer from main and process */
	err = pthread_create(&(tid[5]), NULL, &dispatch_thread, NULL);
	if (err != 0)
		printf("\ncan't create thread :[%s]", strerror(err));
}


/*

	Remember no need to release the buffer of frame

*/
void start_to_record(CvCapture *capture, IplImage *frame)
{
	CvVideoWriter *video_writer = NULL;
	int video_count = 0;

	create_video(&video_writer, frame);
	cvWriteFrame(video_writer, frame);

	time_t start;
	time_t end;  
	double time_cost;  
	time(&start);

	printf(">>>>>>>start to record\n");
	while (true) {
		frame = cvQueryFrame(capture);
		if (!frame) {
			printf("\n critical error when capture\n");
			break;
		}
		cvWriteFrame(video_writer, frame);

		time(&end);  
		if (difftime(end,start) > VIDEO_COUNT)
			break;

	}
	cvReleaseVideoWriter(&video_writer);
	motion_detected = false;
	printf(">>>>>>>end of record for %d seconds\n", VIDEO_COUNT);

}


int main(int argc,char **argv)
{

	IplImage *frame = NULL;
	CvCapture *capture = NULL;

	rhm_prepare_capture(&capture);
	rhm_start_threads();


	while (true) {

		/* query frame, on error exit */
		frame = cvQueryFrame(capture);
		if (!frame) {
			printf("\n critical error when capture\n");
			break;
		}


		if (false == motion_detected) {
			/* enqueue a frame, must be copied before enqueued */
			frame_lock.lock();
			IplImage *frame_copy = cvCreateImage(cvGetSize(frame),frame->depth,3);
			cvCopy(frame, frame_copy);
			webcam_buf.push(frame_copy);
			frame_lock.unlock();
			sleep(1);
		} else {
			start_to_record(capture, frame);
		}

	}

	/* do not forget to release the resource */
	cvReleaseCapture(&capture);

	return 0;
}
