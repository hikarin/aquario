#include <string.h>

#define TEST_RESULT_SUCCESS (0)
#define TEST_RESULT_FALSE   (1)

int main(int argc, char *argv[])
{
    if(argc != 3)
	return TEST_RESULT_FALSE;

    if(strcmp(argv[1], argv[2]) != 0)
	return TEST_RESULT_FALSE;
    
    return TEST_RESULT_SUCCESS;
}
