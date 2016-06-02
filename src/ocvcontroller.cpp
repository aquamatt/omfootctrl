/*******************************************************************************

  Copyright (C) 2016 Francisco Salomón <fsalomon.ing@gmail.com>
  
  This program is free software; you can redistribute it and/or modify it under
  the terms of the GNU General Public License as published by the Free Software
  Foundation, version 2 of the License.

*******************************************************************************/
#include "ocvcontroller.hpp"
#include "main_config.hpp"

#include <libv4l2.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include <chrono>

OCVController::OCVController(const int camdev, const int  baudrate,  
               const string map, const bool nogui, const int guiport,  
               const string defoscserv, const int expressiondiv, const bool verb)  throw(ExOCVController)
{
  namespace bip = boost::asio::ip;
  verbose = verb;
  noGUI = noGUI;
  try
  {

    // Initialize structuring elements for morphological operations
    morphERODE  = getStructuringElement( MORPH_RECT, Size(ERODE_RECT_PIXEL,ERODE_RECT_PIXEL));
    morphDILATE = getStructuringElement( MORPH_RECT, Size(DILATE_RECT_PIXEL,DILATE_RECT_PIXEL));

    // auto exposure control
    if (disable_exposure_auto_priority(camdev) != 0)
      throw(ExOCVController("Not possible to disable auto priority")); 
    
    // open video to write
    //~ videoOut.open("sample.raw", CV_FOURCC('V','P','8','0'), 30.0, Size(FRAME_HEIGHT, FRAME_WIDTH), true);
    videoOut.open("sample.avi", 0,  30.0, Size(FRAME_WIDTH, FRAME_HEIGHT));
    if (!videoOut.isOpened())
      throw(ExOCVController("Not possible to open write video")); 

    //open capture object at location zero (default location for webcam)
    videoCap.open(camdev);
    cb_tpoints.set_capacity(CB_CAPACITY); // =  new boost::circular_buffer<Point>(CB_CAPACITY);

    //set height and width of capture frame
    videoCap.set(CV_CAP_PROP_FRAME_WIDTH,FRAME_WIDTH);
    videoCap.set(CV_CAP_PROP_FRAME_HEIGHT,FRAME_HEIGHT);
    // Get commnds map and its iterator       
    cmap = new CmdMap(map);
    aBank = cmap->getFirstBank(); 
    cmap->printSelBank(verbose);
    // Create midi device
    // TODO reenable midi
    //midiDev = new MIDI(MIDI_CLIENT_NAME, expressiondiv, verbose);
    // Create osc device
    oscDev = new OSC(defoscserv, expressiondiv, verbose);
    // Create an UDP socket for GUI in localhost
    //~ if (!noGUI){        
      //~ guiEndpoint = bip::udp::endpoint(bip::address::from_string("127.0.0.1"), guiport);
      //~ boost::asio::io_service ioService;
      //~ socketGUI = new bip::udp::socket(ioService);
      //~ socketGUI->open(bip::udp::v4());
    //~ }
  }
  catch (const exception &e)
  {
    throw(ExOCVController(e.what())); 
  }
}

// Process input method
void OCVController::processInput(void)
{
  auto start = chrono::steady_clock::now();
  bool sendExpression2GUI = false;
  Mat camFeed, procHSV, procThreshold;
  //~ Mat canvas(FRAME_HEIGHT, FRAME_WIDTH, CV_8U); 
  Mat canvas = Mat::zeros(FRAME_HEIGHT, FRAME_WIDTH, CV_8UC3);
    // Get commnds map and it
  if (!videoCap.read(camFeed)) {
    cerr << "VideoCapture is not reading" << endl;
    return;
  }
  auto tic1 = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - start);
  //flip image
  flip(camFeed, camFeed, 1);
  //videoOut.write(camFeed);
  //convert frame from BGR to HSV colorspace
  cvtColor(camFeed, procHSV, COLOR_BGR2HSV);
  //filter HSV image between values and store filtered image to threshold matrix
  inRange(procHSV,Scalar(H_MIN,S_MIN,V_MIN),Scalar(H_MAX,S_MAX,V_MAX),procThreshold);
  //perform morphological operations on thresholded image to eliminate noise and emphasize the filtered object(s)
  erodeAndDilate(procThreshold);
  //pass in thresholded frame to our object tracking function
  //this function will return the x and y coordinates of the
  //filtered object
  auto tic2 = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - start);
    
  if (trackAndEval(procThreshold, canvas)) {
    try {
      // OSC command
      if(verbose) cout << "Send OSC command: " << RECORD_CMD.name << endl;
      sendExpression2GUI = oscDev->parseAndSendMess("1", RECORD_CMD);
    } catch(ExOSC& e) {
      cerr<< e.what() <<endl;
    }
  } 
  //delay so that screen can refresh.
  #ifdef SHOW_WIN
    imshow("OM OpenCV - feed", camFeed);
    imshow("OM OpenCV - threshold", procThreshold);
  #endif
  drawCmdAreas(canvas);
  imshow("OM OpenCV - Canvas", canvas);
  //image will not appear without this waitKey() command
  waitKey(CV_DELAY_MS);
  auto tic3 = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - start);
  cout << "processInput: " << tic1.count() << " - " << tic2.count()<< " - " << tic3.count()<< endl;
}

void OCVController::drawObject(int area, Point point, Mat &frame){
  //use some of the openCV drawing functions to draw crosshairs on your tracked image!
  circle(frame,point,4,Scalar(0,255,0),-1);
  //circle(frame,Point(x,y),20,Scalar(0,255,0),2);
  //~ if(point.y-25>0) line(frame, point, Point(point.x,point.y-25), Scalar(0,255,0),2);
  //~ else line(frame, point, Point(point.x, 0), Scalar(0,255,0),2);
  //~ 
  //~ if(point.y+25<FRAME_HEIGHT) line(frame, point, Point(point.x, point.y+25), Scalar(0,255,0),2);
  //~ else line(frame, point, Point(x, FRAME_HEIGHT), Scalar(0,255,0),2);
  //~ 
  //~ if(point.x-25>0) line(frame, point, Point(point.x-25,point.y), Scalar(0,255,0),2);
  //~ else line(frame, point, Point(0,point.y), Scalar(0,255,0),2);
  //~ 
  //~ if(point.x+25<FRAME_WIDTH) line(frame, point, Point(point.x+25, point.y), Scalar(0,255,0),2);
  //~ else line(frame, point, Point(FRAME_WIDTH, point.y), Scalar(0,255,0),2);
//~ 
  //~ //  putText(frame,intToString(x)+","+intToString(y)+"\n"+area,Point(x,y+30),1,1,Scalar(0,255,0),2);
  putText(frame, to_string(point.x) + ","+ to_string(point.y) + "\n" + to_string(area), Point(point.x, point.y+30), 1, 1, Scalar(0,255,0),2);
}


void OCVController::drawCmdAreas(Mat &frame){
  //~ line(frame, Point(0, FRAME_HEIGHT/3), Point(FRAME_WIDTH, FRAME_HEIGHT/3), Scalar(255,255,0),2);
  //~ line(frame, Point(0, 2*FRAME_HEIGHT/3), Point(FRAME_WIDTH, 2*FRAME_HEIGHT/3), Scalar(255,255,0),2);

  // Middle line
  line(frame, Point(FRAME_WIDTH/4, 2*FRAME_HEIGHT/3), Point(FRAME_WIDTH, 2*FRAME_HEIGHT/3), Scalar(255,255,0),2);
  // A
  line(frame, Point(2*FRAME_WIDTH/4, 2*FRAME_HEIGHT/3), Point(2*FRAME_WIDTH/4, FRAME_HEIGHT), Scalar(255,255,0),2);
  // B
  line(frame, Point(3*FRAME_WIDTH/4, 2*FRAME_HEIGHT/3), Point(3*FRAME_WIDTH/4, FRAME_HEIGHT), Scalar(255,255,0),2);
  
  // Expression line
  line(frame, Point(FRAME_WIDTH/4, 0), Point(FRAME_WIDTH/4, FRAME_HEIGHT), Scalar(255,255,0),2);
}


void OCVController::erodeAndDilate(Mat &frame){
  erode(frame, frame, morphERODE, Point(-1,-1), ERODE_DILATE_ITS);
  //~ erode(procThreshold, procThreshold, erodeElement);
  //~ dilate(procThreshold, procThreshold, dilateElement);
  dilate(frame, frame, morphDILATE, Point(-1,-1), ERODE_DILATE_ITS);
  //TODO maybe a blur filter is faster than this... medianBLur?
}


bool OCVController::trackAndEval(Mat &threshold, Mat &canvas){
  Mat temp;
  threshold.copyTo(temp);
  //these two vectors needed for output of findContours
  vector< vector<Point> > contours;
  vector<Vec4i> hierarchy;
  //find contours of filtered image using openCV findContours function
  findContours(temp, contours, hierarchy, CV_RETR_CCOMP, CV_CHAIN_APPROX_SIMPLE );
  //use moments method to find our filtered object
  bool retValue = false;
  double refArea = 0;
  double area;
  bool objectFound = false;
  int numObjects = hierarchy.size();
  if (numObjects > 0) {
    //if number of objects greater than MAX_NUM_OBJECTS we have a noisy filter
    if (numObjects<MAX_NUM_OBJECTS){
      for (int index = 0; index >= 0; index = hierarchy[index][0]) {
        Moments moment = moments((Mat)contours[index]);
        area = moment.m00;
        //if the area is less than 20 px by 20px then it is probably just noise
        //if the area is the same as the 3/2 of the image size, probably just a bad filter
        //we only want the object with the largest area so we safe a reference area each
        //iteration and compare it to the area in the next iteration.
        if (area>MIN_OBJECT_AREA && area<MAX_OBJECT_AREA && area>refArea){ // TODO Evaluate this restictions
          Point lastPoint(
            moment.m10/area, // x
            moment.m01/area  // y
          );
          objectFound = true;
          refArea = area;
          cb_tpoints.push_back(lastPoint);
          /* TODO 
           * 1- Llenar un buffer con info de los últimos puntos trackeados.
           * 2- Analizar la info en cada pasada: Cuantos puntos válidos hay? Hubo movimiento? Filtrar algo?
           * 3- Si hubo movimiento, retornar true, setear un inhibicion despues de detectar evento. Timer? 
           * 4- Levantar inhibicion y arrancar de nuevo a evaluar
           */
          #ifdef SHOW_WIN
            //~ drawCmdAreas(threshold);
            drawObject(area, lastPoint, canvas);
          #endif          
          // 
          // !!!!!!!!!!       TODO ver phase corr !!!! puede ser la solución, analizando dos thresholdeadas!!!!!!!!!!
          // 
          if (cb_tpoints.full()) {
            // analize expresion!
            // dtf() <- how to analize that! remember is an array of points!
            // http://docs.opencv.org/2.4/modules/imgproc/doc/motion_analysis_and_object_tracking.html
            vector<double> speed;
            //cout << "HOla, llene el buffer" << endl;
            for (int i=1; i<CB_CAPACITY; i++) {
              //speed.push_back(());
            }
            //~ if () {
              //~ cb_tpoints.clear(); // reset buffer
              //~ retValue = true;
            //~ }
          }
        }
        else 
          cb_tpoints.clear(); // reset buffer
      }
    }
    else {
      cb_tpoints.clear();
      putText(canvas,"TOO MUCH NOISE! ADJUST FILTER",Point(0,50),1,2,Scalar(0,0,255),2);
    }
    return retValue;
  }
}


int OCVController::disable_exposure_auto_priority(const int dev) 
{
  string camdev = "/dev/video" + to_string(dev);
  int descriptor = v4l2_open(camdev.c_str(), O_RDWR);

  v4l2_control c;   // auto exposure control to aperture priority 
  c.id = V4L2_CID_EXPOSURE_AUTO;
  c.value = V4L2_EXPOSURE_APERTURE_PRIORITY; 
  if (v4l2_ioctl(descriptor, VIDIOC_S_CTRL, &c)!=0)
    return -1;
  
  c.id = V4L2_CID_EXPOSURE_AUTO_PRIORITY; // auto priority control to false
  c.value = 0;
  if (v4l2_ioctl(descriptor, VIDIOC_S_CTRL, &c)!=0)
    return -1;
  
  v4l2_close(descriptor);
  return 0;
}
