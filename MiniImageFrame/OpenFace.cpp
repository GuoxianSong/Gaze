#include "OpenFace.h"
// Libraries for landmark detection (includes CLNF and CLM modules)


#define INFO_STREAM( stream ) \
std::cout << stream << std::endl

#define WARN_STREAM( stream ) \
std::cout << "Warning: " << stream << std::endl

#define ERROR_STREAM( stream ) \
std::cout << "Error: " << stream << std::endl

void OpenFace::printErrorAndAbort(const std::string & error)
{
	std::cout << error << std::endl;
	abort();
}




OpenFace::OpenFace(void)
{
	displaywidget_ = new DisplayWidget();
}

OpenFace::~OpenFace()
{
}

void OpenFace::Init(int argc, char **argv)
{
	#define FATAL_STREAM( stream ) \
	printErrorAndAbort( std::string( "Fatal error: " ) + stream )

	main(argc, argv);

}

vector<string> OpenFace::get_arguments(int argc, char **argv)
{

	vector<string> arguments;

	for (int i = 0; i < argc; ++i)
	{
		arguments.push_back(string(argv[i]));
	}
	return arguments;
}

// Visualising the results
void OpenFace::visualise_tracking(cv::Mat& captured_image, cv::Mat_<float>& depth_image, const LandmarkDetector::CLNF& face_model, const LandmarkDetector::FaceModelParameters& det_parameters, cv::Point3f gazeDirection0, cv::Point3f gazeDirection1, int frame_count, double fx, double fy, double cx, double cy)
{

	// Drawing the facial landmarks on the face and the bounding box around it if tracking is successful and initialised
	double detection_certainty = face_model.detection_certainty;
	bool detection_success = face_model.detection_success;

	double visualisation_boundary = 0.2;

	// Only draw if the reliability is reasonable, the value is slightly ad-hoc
	if (detection_certainty < visualisation_boundary)
	{
		//LandmarkDetector::Draw(captured_image, face_model);

		double vis_certainty = detection_certainty;
		if (vis_certainty > 1)
			vis_certainty = 1;
		if (vis_certainty < -1)
			vis_certainty = -1;

		vis_certainty = (vis_certainty + 1) / (visualisation_boundary + 1);

		// A rough heuristic for box around the face width
		int thickness = (int)std::ceil(2.0* ((double)captured_image.cols) / 640.0);

		cv::Vec6d pose_estimate_to_draw = LandmarkDetector::GetCorrectedPoseWorld(face_model, fx, fy, cx, cy);

		// Draw it in reddish if uncertain, blueish if certain
		LandmarkDetector::DrawBox(captured_image, pose_estimate_to_draw, cv::Scalar((1 - vis_certainty)*255.0, 0, vis_certainty * 255), thickness, fx, fy, cx, cy);

		if (det_parameters.track_gaze && detection_success && face_model.eye_model)
		{
			FaceAnalysis::DrawGaze(captured_image, face_model, gazeDirection0, gazeDirection1, fx, fy, cx, cy);
		}
	}

	// Work out the framerate
	if (frame_count % 10 == 0)
	{
		double t1 = cv::getTickCount();
		fps_tracker = 10.0 / (double(t1 - t0) / cv::getTickFrequency());
		t0 = t1;
	}

	// Write out the framerate on the image before displaying it
	char fpsC[255];
	std::sprintf(fpsC, "%d", (int)fps_tracker);
	string fpsSt("FPS:");
	fpsSt += fpsC;
	cv::putText(captured_image, fpsSt, cv::Point(10, 20), CV_FONT_HERSHEY_SIMPLEX, 0.5, CV_RGB(255, 0, 0));

	if (!det_parameters.quiet_mode)
	{
		cv::namedWindow("tracking_result", 1);
		cv::imshow("tracking_result", captured_image);

		if (!depth_image.empty())
		{
			// Division needed for visualisation purposes
			imshow("depth", depth_image / 2000.0);
		}

	}
}

int OpenFace::main(int argc, char **argv)
{

	vector<string> arguments = get_arguments(argc, argv);

	// Some initial parameters that can be overriden from command line	
	vector<string> files, depth_directories, output_video_files, out_dummy;

	// By default try webcam 0

	int device = 0;

	LandmarkDetector::FaceModelParameters det_parameters(arguments);

	// Get the input output file parameters

	// Indicates that rotation should be with respect to world or camera coordinates
	bool u;
	string output_codec;
	LandmarkDetector::get_video_input_output_params(files, depth_directories, out_dummy, output_video_files, u, output_codec, arguments);

	// The modules that are being used for tracking
	LandmarkDetector::CLNF clnf_model(det_parameters.model_location);

	// Grab camera parameters, if they are not defined (approximate values will be used)
	float fx = 0, fy = 0, cx = 0, cy = 0;
	// Get camera parameters
	LandmarkDetector::get_camera_params(device, fx, fy, cx, cy, arguments);

	 

	// If cx (optical axis centre) is undefined will use the image size/2 as an estimate
	bool cx_undefined = false;
	bool fx_undefined = false;
	if (cx == 0 || cy == 0)
	{
		cx_undefined = true;
	}
	if (fx == 0 || fy == 0)
	{
		fx_undefined = true;
	}

	// If multiple video files are tracked, use this to indicate if we are done
	bool done = false;
	int f_n = -1;

	det_parameters.track_gaze = true;

	while (!done) // this is not a for loop as we might also be reading from a webcam
	{

		string current_file;

		// We might specify multiple video files as arguments
		if (files.size() > 0)
		{
			f_n++;
			current_file = files[f_n];
		}
		else
		{
			// If we want to write out from webcam
			f_n = 0;
		}

		bool use_depth = !depth_directories.empty();

		// Do some grabbing
		cv::VideoCapture video_capture;
		if (current_file.size() > 0)
		{
			if (!boost::filesystem::exists(current_file))
			{
				FATAL_STREAM("File does not exist");
				return 1;
			}

			current_file = boost::filesystem::path(current_file).generic_string();

			INFO_STREAM("Attempting to read from file: " << current_file);
			video_capture = cv::VideoCapture(current_file);
		}
		else
		{
			INFO_STREAM("Attempting to capture from device: " << device);
			video_capture = cv::VideoCapture(device);

			// Read a first frame often empty in camera
			cv::Mat captured_image;
			video_capture >> captured_image;
		}

		if (!video_capture.isOpened())
		{
			FATAL_STREAM("Failed to open video source");
			return 1;
		}
		else INFO_STREAM("Device or file opened");

		cv::Mat captured_image;
		video_capture >> captured_image;

		// If optical centers are not defined just use center of image
		if (cx_undefined)
		{
			cx = captured_image.cols / 2.0f;
			cy = captured_image.rows / 2.0f;
		}
		// Use a rough guess-timate of focal length
		if (fx_undefined)
		{
			fx = 500 * (captured_image.cols / 640.0);
			fy = 500 * (captured_image.rows / 480.0);

			fx = (fx + fy) / 2.0;
			fy = fx;
		}

		int frame_count = 0;

		// saving the videos
		cv::VideoWriter writerFace;
		if (!output_video_files.empty())
		{
			try
			{
				writerFace = cv::VideoWriter(output_video_files[f_n], CV_FOURCC(output_codec[0], output_codec[1], output_codec[2], output_codec[3]), 30, captured_image.size(), true);
			}
			catch (cv::Exception e)
			{
				WARN_STREAM("Could not open VideoWriter, OUTPUT FILE WILL NOT BE WRITTEN. Currently using codec " << output_codec << ", try using an other one (-oc option)");
			}
		}

		// Use for timestamping if using a webcam
		int64 t_initial = cv::getTickCount();

		INFO_STREAM("Starting tracking");

		//Draw dot on the screen
		cv::namedWindow("Estimate", CV_WINDOW_NORMAL);		
		cv::Mat img(cv::Mat(1080, 1920, CV_8U));
		img = cv::Scalar(50);    // or the desired uint8_t value from 0-255

		while (!captured_image.empty())
		{

			// Reading the images
			cv::Mat_<float> depth_image;
			cv::Mat_<uchar> grayscale_image;

			if (captured_image.channels() == 3)
			{
				cv::cvtColor(captured_image, grayscale_image, CV_BGR2GRAY);
			}
			else
			{
				grayscale_image = captured_image.clone();
			}

			// Get depth image
			if (use_depth)
			{
				char* dst = new char[100];
				std::stringstream sstream;

				sstream << depth_directories[f_n] << "\\depth%05d.png";
				sprintf(dst, sstream.str().c_str(), frame_count + 1);
				// Reading in 16-bit png image representing depth
				cv::Mat_<short> depth_image_16_bit = cv::imread(string(dst), -1);

				// Convert to a floating point depth image
				if (!depth_image_16_bit.empty())
				{
					depth_image_16_bit.convertTo(depth_image, CV_32F);
				}
				else
				{
					WARN_STREAM("Can't find depth image");
				}
			}

			// The actual facial landmark detection / tracking
			bool detection_success = LandmarkDetector::DetectLandmarksInVideo(grayscale_image, depth_image, clnf_model, det_parameters);

			// Visualising the results
			// Drawing the facial landmarks on the face and the bounding box around it if tracking is successful and initialised
			double detection_certainty = clnf_model.detection_certainty;

			// Gaze tracking, absolute gaze direction
			cv::Point3f gazeDirection0(0, 0, -1);
			cv::Point3f gazeDirection1(0, 0, -1);
			cv::Point3f left_eyeball_center(0, 0, -1);
			cv::Point3f right_eyeball_center(0, 0, -1);

			if (det_parameters.track_gaze && detection_success && clnf_model.eye_model)
			{
				FaceAnalysis::EstimateGaze(left_eyeball_center, clnf_model, gazeDirection0, fx, fy, cx, cy, true); //Left Eyes
				FaceAnalysis::EstimateGaze(right_eyeball_center, clnf_model, gazeDirection1, fx, fy, cx, cy, false); //Right Eye
			}

			

			visualise_tracking(captured_image, depth_image, clnf_model, det_parameters, gazeDirection0, gazeDirection1, frame_count, fx, fy, cx, cy);
			
		
			cv::Point2d dot;
			if (displaywidget_->DotEstimate(gazeDirection0, gazeDirection1, left_eyeball_center, right_eyeball_center, dot))
			{
				circle(img, dot, 32.0, cv::Scalar(0, 0, 255), -1, 8);
				cv::imshow("Estimate", img);
				img = cv::Scalar(50);
			}
			// output the tracked video
			if (!output_video_files.empty())
			{
				//writerFace << captured_image;
			}


			video_capture >> captured_image;

			// detect key presses
			char character_press = cv::waitKey(1);

			// restart the tracker
			if (character_press == 'r')
			{
				clnf_model.Reset();
			}
			// quit the application
			else if (character_press == 'q')
			{
				return(0);
			}

			// Update the frame count
			frame_count++;

		}

		frame_count = 0;

		// Reset the model, for the next video
		clnf_model.Reset();

		// break out of the loop if done with all the files (or using a webcam)
		if (f_n == files.size() - 1 || files.empty())
		{
			done = true;
		}
	}

	return 0;
}

int OpenFace::img_track(int argc, char **argv)
{
	vector<string> arguments = get_arguments(argc, argv);
	// Some initial parameters that can be overriden from command line	
	vector<string> files, depth_directories, output_video_files, out_dummy;
	// By default try webcam 0
	int device = 0;
	LandmarkDetector::FaceModelParameters det_parameters(arguments);
	// Get the input output file parameters

	// Indicates that rotation should be with respect to world or camera coordinates
	bool u;
	string output_codec;
	LandmarkDetector::get_video_input_output_params(files, depth_directories, out_dummy, output_video_files, u, output_codec, arguments);

	// The modules that are being used for tracking
	LandmarkDetector::CLNF clnf_model(det_parameters.model_location);

	// Grab camera parameters, if they are not defined (approximate values will be used)
	float fx = 0, fy = 0, cx = 0, cy = 0;
	// Get camera parameters
	LandmarkDetector::get_camera_params(device, fx, fy, cx, cy, arguments);
	// If cx (optical axis centre) is undefined will use the image size/2 as an estimate
	bool cx_undefined = false;
	bool fx_undefined = false;
	if (cx == 0 || cy == 0)
	{
		cx_undefined = true;
	}
	if (fx == 0 || fy == 0)
	{
		fx_undefined = true;
	}

	// If multiple video files are tracked, use this to indicate if we are done
	bool done = false;
	int f_n = -1;

	det_parameters.track_gaze = true;

	cv::Mat captured_image;
	cv::Mat image;
	image = cv::imread("F:\\Work\\Git\\Gaze\\img\\me.jpg", 0);
	captured_image = image;
	// If optical centers are not defined just use center of image
	if (cx_undefined)
	{
		cx = captured_image.cols / 2.0f;
		cy = captured_image.rows / 2.0f;
	}
	// Use a rough guess-timate of focal length
	if (fx_undefined)
	{
		fx = 500 * (captured_image.cols / 640.0);
		fy = 500 * (captured_image.rows / 480.0);

		fx = (fx + fy) / 2.0;
		fy = fx;
	}

	int frame_count = 0;

	// Reading the images
	cv::Mat_<float> depth_image;
	cv::Mat_<uchar> grayscale_image;

	if (captured_image.channels() == 3)
	{
		cv::cvtColor(captured_image, grayscale_image, CV_BGR2GRAY);
	}
	else
	{
		grayscale_image = captured_image.clone();
	}


	// The actual facial landmark detection / tracking
	bool detection_success = LandmarkDetector::DetectLandmarksInVideo(grayscale_image, depth_image, clnf_model, det_parameters);

	// Visualising the results
	// Drawing the facial landmarks on the face and the bounding box around it if tracking is successful and initialised
	double detection_certainty = clnf_model.detection_certainty;

	// Gaze tracking, absolute gaze direction, eyeball center
	cv::Point3f gazeDirection0(0, 0, -1);
	cv::Point3f gazeDirection1(0, 0, -1);

	
	cv::Point3f left_eyeball_center(0, 0, -1);
	cv::Point3f right_eyeball_center(0, 0, -1);

	if (det_parameters.track_gaze && detection_success && clnf_model.eye_model)
	{
		FaceAnalysis::EstimateGaze(left_eyeball_center, clnf_model, gazeDirection0, fx, fy, cx, cy, true); //Left Eye
		FaceAnalysis::EstimateGaze(right_eyeball_center, clnf_model, gazeDirection1, fx, fy, cx, cy, false); //Right Eye
	}

	visualise_tracking(captured_image, depth_image, clnf_model, det_parameters, gazeDirection0, gazeDirection1, frame_count, fx, fy, cx, cy);
	
	cv::Point2d dot;
	if (displaywidget_->DotEstimate(gazeDirection0, gazeDirection1, left_eyeball_center, right_eyeball_center, dot))
	{
		draw_point(dot);
	}
	return 0;
}

void OpenFace::Debug()
{
	//draw_point(cv::Point( 1920 / 2, 1080 / 2));
	// 31*17cm 10cm

}

void OpenFace::draw_point(cv::Point center)
{
	cv::namedWindow("estimate",CV_WINDOW_NORMAL);
	cv::Mat img = cv::cvarrToMat(cvCreateImage(cvSize(1920, 1080), 8, 3));
	circle(img, center, 32.0, cv::Scalar(0, 0, 255), -1, 8);
	cv::imshow("estimate", img);

}