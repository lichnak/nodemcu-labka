//bruteforce alg
/*
size_t strlen(const char * str)
{
    const char *s;
    for (s = str; *s; ++s) {}
    return(s - str);
}
void my_strlen(const char *str, size_t *len)
{
    for (*len = 0; str[*len]; (*len)++);
}
*/

#include <iostream>
#include <string.h>
using namespace std;

void search(char *s, char *t)
{
    int i=0, j=0, m=strlen(s), n=strlen(t);
    while (i<n)
    {
        while (j<m && i<n)
        {
            if(t[i] != s[j])
            {
                i-=j;
                j=-1;
            }
            i++;j++;
        }
        if(j==m)
        {
            cout << "vysledek: " << i-m << endl;
        }
        j=0;
    }
}

int main()
{
    char *text = "abrakadabraka", *sample="rak";
    search(sample,text)
    system("PAUSE");
    return 0;
}