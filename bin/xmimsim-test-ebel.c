/*
Copyright (C) 2016-2017 Tom Schoonjans and Laszlo Vincze

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include "xmi_ebel.h"


int main(int argc, char *argv[]) {


	double tube_voltage = 30.0;
	xmi_layer tube_anode, tube_window, tube_filter;
	double tube_current = 1.0;
	double tube_angle_electron = 45.0;
	double tube_angle_xray= 6.0;
	double tube_delta_energy = 0.5;

	xmi_excitation *excitation;
	int tube_transmission = 0;

	tube_anode.n_elements = 1;
	tube_anode.Z = g_malloc(sizeof(int));
	tube_anode.weight = g_malloc(sizeof(double));
	tube_anode.Z[0] = 29;
	tube_anode.weight[0] = 1.0;
	tube_anode.density = 8.96;
	tube_anode.thickness= 0.000200;

	tube_window.n_elements = 1;
	tube_window.Z = g_malloc(sizeof(int));
	tube_window.weight = g_malloc(sizeof(double));
	tube_window.Z[0] = 4;
	tube_window.weight[0] = 1.0;
	tube_window.thickness = 0.0125;
	tube_window.density = 1.848;

	tube_filter.n_elements = 1;
	tube_filter.Z = g_malloc(sizeof(int));
	tube_filter.weight = g_malloc(sizeof(double));
	tube_filter.Z[0] = 13;
	tube_filter.weight[0] = 1.0;
	tube_filter.thickness = 0.0870;
	tube_filter.density = 2.7;




	//xmi_tube_ebel(&tube_anode, &tube_window, &tube_filter, tube_voltage, tube_current, tube_angle_electron, tube_angle_xray, 1.0,
	xmi_tube_ebel(&tube_anode, NULL, NULL, tube_voltage, tube_current, tube_angle_electron, tube_angle_xray, tube_delta_energy, 1.0, tube_transmission, &excitation);

	int i;

	fprintf(stdout, "Continuous energies\n");

	for (i = 0 ; i < excitation->n_continuous ; i++)
		fprintf(stdout, "%lf %lf %lf %lf %lf %lf %lf\n", excitation->continuous[i].energy,
			excitation->continuous[i].horizontal_intensity,
			excitation->continuous[i].vertical_intensity,
			excitation->continuous[i].sigma_x,
			excitation->continuous[i].sigma_y,
			excitation->continuous[i].sigma_xp,
			excitation->continuous[i].sigma_yp);


	fprintf(stdout, "Discrete energies\n");

	for (i = 0 ; i < excitation->n_discrete ; i++)
		fprintf(stdout, "%lf %lf %lf %lf %lf %lf %lf\n", excitation->discrete[i].energy,
			excitation->discrete[i].horizontal_intensity,
			excitation->discrete[i].vertical_intensity,
			excitation->discrete[i].sigma_x,
			excitation->discrete[i].sigma_y,
			excitation->discrete[i].sigma_xp,
			excitation->discrete[i].sigma_yp);

	return 0;

}
