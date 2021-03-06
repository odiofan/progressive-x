#include <vector>
#include <thread>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <Eigen/Eigen>

#include "progx_utils.h"
#include "utils.h"
#include "GCoptimization.h"
#include "grid_neighborhood_graph.h"
#include "flann_neighborhood_graph.h"
#include "uniform_sampler.h"
#include "prosac_sampler.h"
#include "progressive_napsac_sampler.h"
#include "fundamental_estimator.h"
#include "homography_estimator.h"
#include "essential_estimator.h"

#include "progressive_x.h"
#include "progress_visualizer.h"

#include <ctime>
#include <direct.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <mutex>
#include <glog/logging.h>

struct stat info;

enum Problem { Homography, TwoViewMotion, RigidMotion };

void testMultiHomographyFitting(
	const std::string &scene_name_, // The name of the current scene 
	const std::string &source_path_, // The path of the source image
	const std::string &destination_path_, // The path of the destination image
	const std::string &input_correspondence_path_, // The path of the detected correspondences
	const std::string &output_correspondence_path_,  // The path of the correspondences saved with their labels
	const std::string &output_match_image_path_, // The path where the images with the labelings are saved
	const double confidence_,
	const double inlier_outlier_threshold_,
	const double spatial_coherence_weight_,
	const double neighborhood_ball_radius_,
	const double maximum_tanimoto_similarity_,
	const size_t minimum_point_number_,
	const bool visualize_results_,
	const bool visualize_inner_steps_,
	const bool has_detected_correspondences_ = false);

bool initializeScene(const std::string &scene_name_,
	std::string &src_image_path_,
	std::string &dst_image_path_,
	std::string &input_correspondence_path_,
	std::string &output_correspondence_path_,
	std::string &output_matched_image_path_,
	const std::string &root_directory = "", // The root directory where the "results" and "data" folder are
	const bool has_detected_correspondences_ = false); // Determine if the correspondences are stored under the data folder or should be detected later

void drawMatches(
	const cv::Mat &points_,
	const std::vector<size_t> &inliers_,
	const cv::Mat &image_src_,
	const cv::Mat &image_dst_,
	cv::Mat &out_image_,
	int circle_radius_,
	const cv::Scalar &color_);

std::mutex writing_mutex;
int settings_number = 0;

std::vector<std::string> getAvailableTestScenes(const Problem &problem_);

int main(int argc, const char* argv[])
{
	// Initialize Google's logging library.
	google::InitGoogleLogging(argv[0]);

	const std::string root_directory = ""; // The directory where the 'data' folder is found

	const bool visualize_results = true, // A flag to tell if the resulting labeling should be visualized
		visualize_inner_steps = false; // A flag to tell if the steps of the algorithm should be visualized
	const size_t minimum_point_number = 14; // The minimum number of inliers needed to accept a model instance
	const double confidence = 0.80, // The required confidence in the results
		maximum_tanimoto_similarity = 0.3,
		neighborhood_ball_radius = 20.0, // The radius of the ball hyper-sphere used for determining the neighborhood graph.
		spatial_coherence_weight = 0.4, // 
		inlier_outlier_threshold = 2.0; // 
	
	for (const std::string &scene : getAvailableTestScenes(Problem::Homography))
	{
		printf("Processed scene = %s.\n", scene.c_str());

		std::string src_image_path, // Path of the source image
			dst_image_path, // Path of the destination image
			input_correspondence_path, // Path where the detected correspondences are saved
			output_correspondence_path, // Path where the inlier correspondences are saved
			output_matched_image_path; // Path where the matched image is saved

		// Initializing the paths 
		initializeScene(scene, // The scene's name
			src_image_path, // The path of the source image
			dst_image_path, // The path of the destination image
			input_correspondence_path, // The path of the detected correspondences
			output_correspondence_path, // The path of the correspondences saved with their labels
			output_matched_image_path, // The path where the images with the labelings are saved
			root_directory, // The root directory where the "results" and "data" folder are
			true); // In this dataset, the correspondences and a reference labeling are provided

		testMultiHomographyFitting(
			scene, // The name of the current scene
			src_image_path, // The source image's path
			dst_image_path, // The destination image's path
			input_correspondence_path, // The path where the detected correspondences (before the robust estimation) will be saved (or loaded from if exists)
			output_correspondence_path, // The path where the inliers of the estimated fundamental matrices will be saved
			output_matched_image_path, // The path where the matched image pair will be saved
			confidence, // The RANSAC confidence value
			inlier_outlier_threshold, // The used inlier-outlier threshold in GC-RANSAC.
			spatial_coherence_weight, // The weight of the spatial coherence term in the graph-cut energy minimization.
			neighborhood_ball_radius, // The radius of the neighborhood ball for determining the neighborhoods.
			maximum_tanimoto_similarity, // The maximum Tanimoto similarity of the proposal and compound instances.
			minimum_point_number, // The minimum number of inlier for a model to be kept.
			visualize_results, // A flag to determine if the results should be visualized
			visualize_inner_steps, // A flag to determine if the inner steps should be visualized.
			true);  // In this dataset, the correspondences and a reference labeling are provided
	}

	return 0;
}

std::vector<std::string> getAvailableTestScenes(const Problem &problem_)
{
	switch (problem_)
	{
	case Problem::Homography:	
		return { "oldclassicswing", "unihouse", "unionhouse" };
	case Problem::TwoViewMotion:
		LOG(WARNING) << "Multiple two-motion fitting is not implemented yet. Coming soon...";
		return {};
	case Problem::RigidMotion:
		LOG(WARNING) << "Multi-motion fitting is not implemented yet. Coming soon...";
		return {};
	default:
		return {};
	}
}

// Initializing the paths regarding the current scene
bool initializeScene(const std::string &scene_name_, // The scene's name
	std::string &src_image_path_, // The path of the source image
	std::string &dst_image_path_, // The path of the destination image
	std::string &input_correspondence_path_, // The path of the detected correspondences
	std::string &output_correspondence_path_, // The path of the correspondences saved with their labels
	std::string &output_matched_image_path_, // The path where the images with the labelings are saved 
	const std::string &root_directory_, // The root directory where the "results" and "data" folder are
	const bool has_detected_correspondences_) // Determine if the correspondences are stored under the data folder or should be detected later
{
	// The directory to which the results will be saved
	std::string dir = "results/" + scene_name_;
	
	// The source image's path
	src_image_path_ =
		root_directory_ + "data/" + scene_name_ + "/" + scene_name_ + "1.jpg";
	if (cv::imread(src_image_path_).empty())
		src_image_path_ = root_directory_ + "data/" + scene_name_ + "/" + scene_name_ + "1.png";
	if (cv::imread(src_image_path_).empty())
	{
		LOG(FATAL) << "Error while loading source image \"" <<
			src_image_path_ << "\"";
		return false;
	}

	// The destination image's path
	dst_image_path_ =
		root_directory_ + "data/" + scene_name_ + "/" + scene_name_ + "2.jpg";
	if (cv::imread(dst_image_path_).empty())
		dst_image_path_ = root_directory_ + "data/" + scene_name_ + "/" + scene_name_ + "2.png";
	if (cv::imread(dst_image_path_).empty())
	{
		LOG(FATAL) << "Error while loading destination image \"" <<
			dst_image_path_ << "\"";
		return false;
	}

	// The path where the detected correspondences (before the robust estimation) will be saved (or loaded from if exists)
	if (has_detected_correspondences_) // If the correspondences are provided with the dataset, load them.
		input_correspondence_path_ = 
			root_directory_ + "data/" + scene_name_ + "/" + scene_name_ + ".txt";
	else // Otherwise, they will be saved under folder "results".
		input_correspondence_path_ =
			"results/" + scene_name_ + "/" + scene_name_ + "_points_with_no_annotation.txt";
	// The path where the inliers of the estimated fundamental matrices will be saved
	output_correspondence_path_ =
		"results/" + scene_name_ + "/result_" + scene_name_ + ".txt";
	// The path where the matched image pair will be saved
	output_matched_image_path_ =
		"results/" + scene_name_ + "/matches_" + scene_name_ + ".png";

	return true;
}

void testMultiHomographyFitting(
	const std::string &scene_name_, // The name of the current scene 
	const std::string &source_path_, // The path of the source image
	const std::string &destination_path_, // The path of the destination image
	const std::string &input_correspondence_path_, // The path of the detected correspondences
	const std::string &output_correspondence_path_,  // The path of the correspondences saved with their labels
	const std::string &output_match_image_path_, // The path where the images with the labelings are saved
	const double confidence_, // The RANSAC confidence value
	const double inlier_outlier_threshold_, // The used inlier-outlier threshold in GC-RANSAC.
	const double spatial_coherence_weight_, // The weight of the spatial coherence term in the graph-cut energy minimization.
	const double neighborhood_ball_radius_, // The radius of the neighborhood ball for determining the neighborhoods.
	const double maximum_tanimoto_similarity_, // The maximum Tanimoto similarity of the proposal and compound instances.
	const size_t minimum_point_number_, // The minimum number of inlier for a model to be kept.
	const bool visualize_results_, // A flag to determine if the results should be visualized
	const bool visualize_inner_steps_, // A flag to determine if the inner steps should be visualized.
	const bool has_detected_correspondences_)
{
	// Read the images
	cv::Mat source_image = cv::imread(source_path_); // The source image
	cv::Mat destination_image = cv::imread(destination_path_); // The destination image

	if (source_image.empty()) // Check if the source image is loaded successfully
	{
		LOG(FATAL) <<
			"An error occured while loading image \"" << source_path_ << "\"";
		return;
	}

	if (destination_image.empty()) // Check if the destination image is loaded successfully
	{
		LOG(FATAL) <<
			"An error occured while loading image \"" << destination_path_ << "\"";
		return;
	}

	// Detect or load point correspondences using AKAZE 
	cv::Mat points;
	std::vector<size_t> reference_labeling;
	size_t reference_model_number = 0;
	if (has_detected_correspondences_)
		loadPointsWithLabels(points,
			reference_labeling,
			reference_model_number,
			input_correspondence_path_.c_str());
	else
		detectFeatures(
			input_correspondence_path_, // The path where the correspondences are read from or saved to.
			source_image, // The source image
			destination_image, // The destination image
			points); // The detected point correspondences. Each row is of format "x1 y1 x2 y2"

	// Initialize the neighborhood used in Graph-cut RANSAC and, perhaps,
	// in the sampler if NAPSAC or Progressive-NAPSAC sampling is applied.
	std::chrono::time_point<std::chrono::system_clock> start, end; // Variables for time measurement
	start = std::chrono::system_clock::now(); // The starting time of the neighborhood calculation
	gcransac::neighborhood::FlannNeighborhoodGraph neighborhood(&points, // All data points
		neighborhood_ball_radius_); // The radius of the neighborhood ball for determining the neighborhoods.
	end = std::chrono::system_clock::now(); // The end time of the neighborhood calculation
	std::chrono::duration<double> elapsed_seconds = end - start; // The elapsed time in seconds

	printf("Neighborhood calculation time = %f secs.\n", elapsed_seconds.count());

	// Calculating the maximal diagonal size of the images.
	// This value will be then used to determine a threshold adaptively,
	// based on the image size.
	const double max_diagonal_length =
		sqrt(pow(MAX(source_image.cols, destination_image.cols), 2) +
			pow(MAX(source_image.rows, destination_image.rows), 2));

	// The main sampler is used inside the local optimization
	gcransac::sampler::ProgressiveNapsacSampler main_sampler(&points, // All data points
		{ 16, 8, 4, 2 }, // The layer structure of the sampler's multiple grids
		gcransac::DefaultHomographyEstimator::sampleSize(), // The size of a minimal sample
		source_image.cols, // The width of the source image
		source_image.rows, // The height of the source image
		destination_image.cols, // The width of the destination image
		destination_image.rows); // The height of the destination image

	// The local optimization sampler is used inside the local optimization
	gcransac::sampler::UniformSampler local_optimization_sampler(&points);

	// Initializing the multi-homography visualizer which will show
	// the results of each step of Progressive-X.
	progx::MultiHomographyVisualizer visualizer(&points, // All data points
		&source_image, // A pointer to the source image
		&destination_image, // A pointer to the destination image
		0.005 * max_diagonal_length); // The radius of the drawn circles

	// Applying Progressive-X
	progx::ProgressiveX<gcransac::neighborhood::FlannNeighborhoodGraph, // The type of the used neighborhood-graph
		gcransac::DefaultHomographyEstimator, // The type of the used model estimator
		gcransac::sampler::ProgressiveNapsacSampler, // The type of the used main sampler in GC-RANSAC
		gcransac::sampler::UniformSampler> // The type of the used sampler in the local optimization of GC-RANSAC
		progressive_x(
			// If the results should be visualized pass the point of the visualizer.
			// Otherwise, set it to a null pointer.
			visualize_inner_steps_ ?
			&visualizer :
			nullptr);

	// Set the parameters of Progressive-X
	progx::MultiModelSettings &settings = progressive_x.getMutableSettings();
	// The minimum number of inlier required to keep a model instance.
	// This value is used to determine the label cost weight in the alpha-expansion of PEARL.
	settings.minimum_number_of_inliers = minimum_point_number_;
	// The inlier-outlier threshold
	settings.inlier_outlier_threshold = inlier_outlier_threshold_;
	// The required confidence in the results
	settings.confidence = confidence_;
	// The maximum Tanimoto similarity of the proposal and compound instances
	settings.maximum_tanimoto_similarity = maximum_tanimoto_similarity_;
	// The weight of the spatial coherence term
	settings.spatial_coherence_weight = spatial_coherence_weight_;

	progressive_x.run(points, // All data points
		neighborhood, // The neighborhood graph
		main_sampler, // The main sampler used in GC-RANSAC
		local_optimization_sampler); // The sampler used in the local optimization of GC-RANSAC

	// Calculate the misclassification error if a reference labeling is known
	double misclassification_error = getMisclassificationError(
		progressive_x.getModels(),
		reference_labeling,
		progressive_x.getModelNumber(),
		reference_model_number);

	printf("Processing time = %f secs.\n", progressive_x.getStatistics().processing_time);
	printf("Misclassification error <= %f\%.\n", misclassification_error);
	printf("Number of found model instances = %d (there are %d instances in the reference labeling).\n", progressive_x.getModelNumber(), reference_model_number);

	// Visualize the final results if needed
	if (visualize_results_)
	{
		// If the visualizer has not been initialize, initialize it.
		if (!visualize_inner_steps_)
			visualizer.setLabeling(&progressive_x.getStatistics().labeling, // The obtained labeling
				progressive_x.getModelNumber() + 1); // The number of labels
		visualizer.visualize(0, // If the delay is 0, it will wait for key press.
			"Resulting labeling"); // The name of the window
	}
	visualizer.release();
	printf("\n");
}

void drawMatches(
	const cv::Mat &points_, 
	const std::vector<size_t> &inliers_,
	const cv::Mat &image_src_,
	const cv::Mat &image_dst_,
	cv::Mat &out_image_,
	int circle_radius_,
	const cv::Scalar &color_)
{
	for (const auto &idx : inliers_)
	{
		cv::Point2d pt1(points_.at<double>(idx, 0),
			points_.at<double>(idx, 1));
		cv::Point2d pt2(image_dst_.cols + points_.at<double>(idx, 2),
			points_.at<double>(idx, 3));
		
		cv::circle(out_image_, pt1, circle_radius_, color_, static_cast<int>(circle_radius_ * 0.4));
		cv::circle(out_image_, pt2, circle_radius_, color_, static_cast<int>(circle_radius_ * 0.4));
		cv::line(out_image_, pt1, pt2, color_, 2);
	}
}