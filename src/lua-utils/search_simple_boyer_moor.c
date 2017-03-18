// zjednoduseny Boyer-Mooreuv alg
#define MAX_ABC 26

#include <iostream>
#include <string.h>
using namespace std;

void vmove(char *s, int move[])
{
    int i, m;
    m=strlen(s);
    for (i=0; i<MAX_ABC;++i)
        move[i] = m;
    for (i=0; i<m-1; ++i)
        move[s[i]-97] = m-i-1;
    for (i=0; i<MAX_ABC; ++i)
        cout << posuv[i] << " ";
    cout << endl;
}

void search(char *s, char *t)
{
    int i,j,m,n,move[MAX_ABC];
    m=strlen(s);
    n=strlen(t);
    
    vmove(v,move);
    
    j=0;
    while (j<=n-m)
    {
        for (i=m-1; i>=0 && s[i] == t[i+j]; --i);
        if (i<0)
        {
            cout << "sample position: " << j << endl;
            j+=m;
        }
        else
            j=j+move[t[i+j]-97]-m+1+i;
    }
}


int main()
{
    int i;
    char *text = "abrakadabraka", *sample="rak";
    search(sample,text)
    system("PAUSE");
    return 0;
}