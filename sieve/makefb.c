#include <stdio.h>
#include <stdlib.h>
#include <string.h> /* for strcmp() */
#include "cado.h"
#include "utils/utils.h"

void
makefb (FILE *fp, cado_poly cpoly)
{
  unsigned long p;
  int d = cpoly->degree;
  LONG *roots, r;
  int nroots, i, j;

  check_polynomials (cpoly);

  fprintf (fp, "# Roots for polynomial ");
  fprint_polynomial (fp, cpoly->f, d);

  fprintf (fp, "# DEGREE: %d\n", d);

  roots = (LONG*) malloc (d * sizeof (LONG));

  for (p = 2; p <= cpoly->alim; p = getprime (p))
    {
      if (long_poly_fits (d, p))
        nroots = roots_mod_long (roots, cpoly->f, d, p);
      else
        nroots = modul_roots_mod_long (roots, cpoly->f, d, &p);
      /* normalize roots in [0, p-1] */
      for (i = 0; i < nroots; i++)
        if (roots[i] < 0)
          roots[i] += p;
      /* sort roots by insertion sort */
      for (i = 1; i < nroots; i++)
        {
          /* assume roots[0]...roots[i-1] are already sorted */
          for (j = i, r = roots[j]; j > 0 && r < roots[j - 1]; j--)
            roots[j] = roots[j - 1];
          roots[j] = r;
        }
      if (nroots != 0)
        {
          fprintf (fp, "%lu: %lld", p, (long long int) roots[0]);
          for (i = 1; i < nroots; i++)
            fprintf (fp, ",%lld", (long long int) roots[i]);
          fprintf (fp, "\n");
        }
    }

  getprime (0); /* free the memory used by getprime() */
  free (roots);
}

int
main (int argc, char *argv[])
{
  char **argv0 = argv;
  int verbose = 0;
  char *polyfilename = NULL;
  cado_poly cpoly;

  while (argc > 1 && argv[1][0] == '-')
    {
      if (argc > 1 && strcmp (argv[1], "-v") == 0)
	{
	  verbose ++;
	  argc --;
	  argv ++;
	}
      else if (argc > 2 && strcmp (argv[1], "-poly") == 0)
	{
	  polyfilename = argv[2];
	  argc -= 2;
	  argv += 2;
	}
      else 
	{
	  fprintf (stderr, "Usage: %s [-v] -poly <file>\n", argv0[0]);
	  exit (EXIT_FAILURE);
	}
    }

  if (polyfilename == NULL)
    {
      fprintf (stderr, "Please specify a polynomial file with -poly\n");
      exit (EXIT_FAILURE);
    }

  if (!read_polynomial (cpoly, polyfilename))
    {
      fprintf (stderr, "Error reading polynomial file\n");
      exit (EXIT_FAILURE);
    }

  makefb (stdout, cpoly);

  return 0;
}
