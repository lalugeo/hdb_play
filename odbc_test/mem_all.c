#include <stdlib.h>
#include <stdio.h>

void main(int argc, char *argv[]){
	double *irr;
	int k;
	int i;

	irr=calloc(1,sizeof(double));

	for(i=0;i<100;i++){

		irr=realloc(irr,(i+1)*sizeof(double));
		*(irr+i)=i;
	}

	
	for(k=0;k<100;k++){
		printf("%f\n",*(irr+k));
	}
}
