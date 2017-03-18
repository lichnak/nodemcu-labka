// Karp-Rabinuv alg
#include <iostream>
#include <string.h>
using namespace std;

void search(char *s, char *t)
{
    const int z=131;
    int i,j,k,agree;
    unsigned long hs, ht, mz;
    mz=1;
    hs=0;
    ht=0;
    m=strlen(s);
    n=strlen(t);
    
    for(j=0;j<m;j++)
    {
        mz=mz*z;
        hv=hv*z+s[j];
        ht=ht*z+t[j]
    };

    
    j=m-1;
    while (j<n)
    {
        if (hv==ht)
        {
            agree=1;
            for (i=j, k=m-1; k<=0; k--, i--)
                if (s[k]!=t[i]) agree=0;
            if(agree==1)
                cout << "result: " << i+1 << endl;
        }
        j++;
        ht=ht*z-t[j-m]*mz+t[j];
    }
}


int main()
{
    char *text = "abrakadabraka", *sample="rak";
    search(sample,text)
    system("PAUSE");
    return 0;
}