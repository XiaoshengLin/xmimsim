#include <config.h>
#include "libxmimsim-test.h"
#include "xmi_msim.h"
#include "xmi_aux.h"
#include "xmi_private.h"
#include <glib.h>
#include <gio/gio.h>
#include <math.h>
#include <unistd.h>

int main(int argc, char *argv[]) {

  // init test
  g_assert(test_init() == 1);

  // file should be present already
  // 1. keep outputfile as is
  g_assert(xmi_xmso_to_xmsi_xslt(TEST_XMSO, "temp.xmsi", NULL) == 1);

  xmi_output *output = NULL;
  g_assert_nonnull(output = xmi_output_read_from_xml_file(TEST_XMSO, NULL));

  xmi_input *input = NULL;
  g_assert_nonnull(input = xmi_input_read_from_xml_file("temp.xmsi", NULL));

  g_assert(xmi_input_compare(input, output->input) == 0);

  xmi_input_free(input);

  // 2. change outputfile
  g_assert(xmi_xmso_to_xmsi_xslt(TEST_XMSO, "temp.xmsi", "new-outputfile.xmso") == 1);
  
  g_assert_nonnull(input = xmi_input_read_from_xml_file("temp.xmsi", NULL));

  g_assert(xmi_input_compare(input, output->input) == XMI_INPUT_GENERAL);
 
  g_assert_cmpstr(input->general->outputfile, ==, "new-outputfile.xmso");

  xmi_input_free(input);
  xmi_output_free(output);
  unlink("temp.xmsi");

  return 0;
}
