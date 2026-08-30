

int factorial(int n) {
    
        // Calculating factorial of number
        int ans = 1;
        for (int i = 2; i <= n; i++) {
            ans = ans * i;
        }
        return ans;
    }

int _start()
{
    int num = 5;
    int val = factorial(num);
    return val;
}