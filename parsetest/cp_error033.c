struct A {
	int a, b;
};

static struct A deduce_conversion(int from, int to)
{
	struct A result = { 1, 2 };
	return result;
}

struct A globa_real;
struct A *globa = &globa_real;

int main(int argc, char **argv)
{
	*globa = deduce_conversion(1, 2);
	return 0;
}
