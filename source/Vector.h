/*
 * Vector.h
 *
 *  Created on: 27 Aug 2014
 *      Author: helenginn
 */

#ifndef VECTOR_H_
#define VECTOR_H_
#include <vector>

#include <boost/tuple/tuple.hpp>
#include "parameters.h"


vec reverseVector(vec vec1);
bool vectors_are_equal(vec vec1, vec vec2);
bool within_vicinity(vec vec1, vec vec2, double maxD);
vec cross_product_for_vectors(vec vec1, vec vec2);
double dot_product_for_vectors(vec vec1, vec vec2);
vec new_vector(double h, double k, double l);
double length_of_vector(vec &vect);
double length_of_vector_squared(vec vec);
double distance_between_vectors(vec vec1, vec vec2);
double angleBetweenVectors(vec vec1, vec vec2, int isUnit = false);
double cosineBetweenVectors(vec vec1, vec vec2);
double cosineBetweenUnitVectors(vec vec1, vec vec2);
vec copy_vector(vec old_vec);
void add_vector_to_vector(vec *vec1, vec vec2);
vec vector_between_vectors(vec vec1, vec vec2);
void take_vector_away_from_vector(vec vec1, vec *vec2);
void multiply_vector(vec *vec, double mult);
void scale_vector_to_distance(vec *vec, double new_distance);
MatrixPtr rotation_between_vectors(vec vec1, vec vec2);

MatrixPtr closest_rotmat_analytical(vec vec1, vec vec2,
                                    vec axis, double *resultantAngle, bool addPi = true);

double getEwaldSphereNoMatrix(vec index);
double cdf(double x, double mean, double sigma);
double _cdf(double x);
double normal_distribution(double x, double mean, double sigma);
double super_gaussian(double x, double mean, double sigma_0, double exponent);

double minimizeParam(double &step, double &param, double (*score)(void *object),
                         void *object);
double minimizeParameter(double &step, double *param, double (*score)(void *object),
                       void *object);

double sum(vector<double> values);
void regression_line(vector<boost::tuple<double, double, double> > values, double &intercept, double &gradient);
double correlation_between_vectors(vector<double> *vec1,
		vector<double> *vec2, vector<double> *weights, int exclude);
double correlation_between_vectors(vector<double> *vec1,
		vector<double> *vec2, vector<double> *weights);
double correlation_between_vectors(vector<double> *vec1,
		vector<double> *vec2);
double correlation_through_origin(vector<double> *vec1,
		vector<double> *vec2, vector<double> *weights = NULL);
double least_squares_between_vectors(vector<double> *vec1,
		vector<double> *vec2, double slope);
double gradient_between_vectors(vector<double> *vec1,
		vector<double> *vec2);
double weighted_mean(vector<double> *means, vector<double> *weights = NULL);
double median(vector<double> *means);
void histogram_gaussian(vector<double> *means, vector<int> *freq, double &mean, double &stdev);
double standard_deviation(vector<double> *values, vector<double> *weights = NULL);
double r_factor_between_vectors(vector<double> *vec1,
		vector<double> *vec2, vector<double> *weights, double scale);
double standard_deviation(vector<double> *values, vector<double> *weights, double mean);
double bitty_deviation(vector<double> *values, vector<double> *weights);
void printDesc(vec hkl);
std::string desc(vec hkl);
std::string prettyDesc(vec hkl);
void closest_major_axis(vec &a_vec);

double cartesian_to_distance(double x, double y);
double cartesian_to_angle(double x, double y);
std::map<double, int> histogram(std::vector<double> values, double step);
void histogramCSV(std::string filename, std::map<double, int> map1, std::map<double, int> map2);

#endif /* VECTOR_H_ */
