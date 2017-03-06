/*
 * Tests for PIO distributed arrays.
 *
 * Ed Hartnett, 3/6/17
 */
#include <pio.h>
#include <pio_internal.h>
#include <pio_tests.h>

/* The number of tasks this test should run on. */
#define TARGET_NTASKS 4

/* The minimum number of tasks this test should run on. */
#define MIN_NTASKS 4

/* The name of this test. */
#define TEST_NAME "test_darray_uneven"

/* Number of processors that will do IO. */
#define NUM_IO_PROCS 1

/* Number of computational components to create. */
#define COMPONENT_COUNT 1

/* The number of dimensions in the example data. In this test, we
 * are using three-dimensional data. */
#define NDIM 4

/* But sometimes we need arrays of the non-record dimensions. */
#define NDIM3 3

/* The length of our sample data along each dimension. */
#define X_DIM_LEN 2
#define Y_DIM_LEN 4
#define Z_DIM_LEN 4

/* This is the length of the map for each task. */
#define EXPECTED_MAPLEN 16

/* The number of timesteps of data to write. */
#define NUM_TIMESTEPS 2

/* The name of the variable in the netCDF output files. */
#define VAR_NAME "foo"

/* Test with and without specifying a fill value to
 * PIOc_write_darray(). */
#define NUM_TEST_CASES_FILLVALUE 2

/* The dimension names. */
char dim_name[NDIM][PIO_MAX_NAME + 1] = {"timestep", "x", "y", "z"};

/* Length of the dimensions in the sample data. */
int dim_len[NDIM] = {NC_UNLIMITED, X_DIM_LEN, Y_DIM_LEN, Z_DIM_LEN};

#define DIM_NAME "dim"
#define NDIM1 1

/* Run test for each of the rearrangers. */
#define NUM_REARRANGERS_TO_TEST 2

/* Create the decomposition to divide the 4-dimensional sample data
 * between the 4 tasks. For the purposes of decomposition we are only
 * concerned with 3 dimensions - we ignore the unlimited dimension.
 *
 * @param ntasks the number of available tasks
 * @param my_rank rank of this task.
 * @param iosysid the IO system ID.
 * @param dim_len an array of length 3 with the dimension sizes.
 * @param ioid a pointer that gets the ID of this decomposition.
 * @returns 0 for success, error code otherwise.
 **/
int create_decomposition_3d(int ntasks, int my_rank, int iosysid, int *dim_len_3d, int *ioid)
{
    PIO_Offset elements_per_pe;     /* Array elements per processing unit. */
    PIO_Offset *compdof;  /* The decomposition mapping. */
    int ret;

    /* How many data elements per task? In this example we will end up
     * with 4. */
    elements_per_pe = X_DIM_LEN * Y_DIM_LEN * Z_DIM_LEN / ntasks;

    /* Allocate space for the decomposition array. */
    if (!(compdof = malloc(elements_per_pe * sizeof(PIO_Offset))))
        return PIO_ENOMEM;

    /* Describe the decomposition. */
    for (int i = 0; i < elements_per_pe; i++)
        compdof[i] = my_rank * elements_per_pe + i;

    /* Create the PIO decomposition for this test. */
    printf("%d Creating decomposition elements_per_pe = %lld\n", my_rank, elements_per_pe);
    if ((ret = PIOc_init_decomp(iosysid, PIO_INT, NDIM3, dim_len_3d, elements_per_pe,
                                compdof, ioid, NULL, NULL, NULL)))
        ERR(ret);

    printf("%d decomposition initialized.\n", my_rank);

    /* Free the mapping. */
    free(compdof);

    return 0;
}

/**
 * Test the decomp read/write functionality.
 *
 * @param iosysid the IO system ID.
 * @param ioid the ID of the decomposition.
 * @param num_flavors the number of IOTYPES available in this build.
 * @param flavor array of available iotypes.
 * @param my_rank rank of this task.
 * @param rearranger the rearranger to use (PIO_REARR_BOX or
 * PIO_REARR_SUBSET).
 * @param test_comm the MPI communicator for this test.
 * @returns 0 for success, error code otherwise.
*/
int test_decomp_read_write(int iosysid, int ioid, int num_flavors, int *flavor, int my_rank,
                           int rearranger, MPI_Comm test_comm)
{
    char filename[PIO_MAX_NAME + 1]; /* Name for the output files. */
    int ioid2;             /* ID for decomposition we will create from file. */
    char title_in[PIO_MAX_NAME + 1];   /* Optional title. */
    char history_in[PIO_MAX_NAME + 1]; /* Optional history. */
    int fortran_order_in; /* Indicates fortran vs. c order. */
    int ret;              /* Return code. */

    /* Use PIO to create the decomp file in each of the four
     * available ways. */
    for (int fmt = 0; fmt < num_flavors; fmt++)
    {
        /* Create the filename. */
        sprintf(filename, "decomp_%s_iotype_%d.nc", TEST_NAME, flavor[fmt]);

        printf("writing decomp file %s\n", filename);
        if ((ret = PIOc_write_nc_decomp(iosysid, filename, 0, ioid, test_comm, NULL,
                                        NULL, 0)))
            return ret;

        /* Read the data. */
        printf("reading decomp file %s\n", filename);
        if ((ret = PIOc_read_nc_decomp(iosysid, filename, &ioid2, test_comm, PIO_INT,
                                       title_in, history_in, &fortran_order_in)))
            return ret;

        /* Check the results. */
        {
            iosystem_desc_t *ios;
            io_desc_t *iodesc;

            /* Get the IO system info. */
            if (!(ios = pio_get_iosystem_from_id(iosysid)))
                return pio_err(NULL, NULL, PIO_EBADID, __FILE__, __LINE__);

            /* Get the IO desc, which describes the decomposition. */
            if (!(iodesc = pio_get_iodesc_from_id(ioid2)))
                return pio_err(ios, NULL, PIO_EBADID, __FILE__, __LINE__);
            if (iodesc->ioid != ioid2 || iodesc->maplen != EXPECTED_MAPLEN || iodesc->ndims != NDIM3 ||
                iodesc->ndof != EXPECTED_MAPLEN)
                return ERR_WRONG;
            if (iodesc->rearranger != rearranger || iodesc->maxregions != 1 ||
                iodesc->needsfill || iodesc->basetype != MPI_INT)
                return ERR_WRONG;
            for (int e = 0; e < iodesc->maplen; e++)
                if (iodesc->map[e] != my_rank * iodesc->maplen + e + 1)
                    return ERR_WRONG;
            if (iodesc->dimlen[0] != X_DIM_LEN || iodesc->dimlen[1] != Y_DIM_LEN ||
                iodesc->dimlen[2] != Z_DIM_LEN)
                return ERR_WRONG;
            if (rearranger == PIO_REARR_SUBSET)
            {
                if (iodesc->nrecvs != 1  || iodesc->num_aiotasks != TARGET_NTASKS)
                    return ERR_WRONG;
            }
            else
            {
                /* I haven't figured out yet what these should be for
                 * the box rearranger. */
                printf("iodesc->nrecv = %d iodesc->num_aiotasks = %d\n", iodesc->nrecvs,
                       iodesc->num_aiotasks);
            }
        }

        /* Free the PIO decomposition. */
        if ((ret = PIOc_freedecomp(iosysid, ioid2)))
            ERR(ret);
    }
    return PIO_NOERR;
}

/* Run tests for darray functions. */
int main(int argc, char **argv)
{
    int my_rank;
    int ntasks;
    int num_flavors; /* Number of PIO netCDF flavors in this build. */
    int flavor[NUM_FLAVORS]; /* iotypes for the supported netCDF IO flavors. */
    MPI_Comm test_comm; /* A communicator for this test. */
    int ret;         /* Return code. */

    /* Initialize test. */
    if ((ret = pio_test_init2(argc, argv, &my_rank, &ntasks, MIN_NTASKS,
                              MIN_NTASKS, 3, &test_comm)))
        ERR(ERR_INIT);

    if ((ret = PIOc_set_iosystem_error_handling(PIO_DEFAULT, PIO_RETURN_ERROR, NULL)))
        return ret;

    /* Only do something on max_ntasks tasks. */
    if (my_rank < TARGET_NTASKS)
    {
        int rearranger[NUM_REARRANGERS_TO_TEST] = {PIO_REARR_BOX, PIO_REARR_SUBSET};
        int iosysid;  /* The ID for the parallel I/O system. */
        int ioproc_stride = 1;    /* Stride in the mpi rank between io tasks. */
        int ioproc_start = 0;     /* Zero based rank of first processor to be used for I/O. */
        int dim_len_3d[NDIM3] = {X_DIM_LEN, Y_DIM_LEN, Z_DIM_LEN};
        int ret;      /* Return code. */

        /* Figure out iotypes. */
        if ((ret = get_iotypes(&num_flavors, flavor)))
            ERR(ret);
        printf("Runnings tests for %d flavors\n", num_flavors);

        for (int r = 0; r < NUM_REARRANGERS_TO_TEST; r++)
        {
            int ioid; /* Decomposition ID. */
            
            /* Initialize the PIO IO system. This specifies how
             * many and which processors are involved in I/O. */
            if ((ret = PIOc_Init_Intracomm(test_comm, TARGET_NTASKS, ioproc_stride,
                                           ioproc_start, rearranger[r], &iosysid)))
                return ret;

            /* Decompose the data over the tasks. */
            if ((ret = create_decomposition_3d(TARGET_NTASKS, my_rank, iosysid, dim_len_3d, &ioid)))
                return ret;
            
            /* Test decomposition read/write. */
            if ((ret = test_decomp_read_write(iosysid, ioid, num_flavors, flavor, my_rank,
                                              rearranger[r], test_comm)))
                return ret;
            
            /* Free the PIO decomposition. */
            if ((ret = PIOc_freedecomp(iosysid, ioid)))
                ERR(ret);
            
            /* Finalize PIO system. */
            if ((ret = PIOc_finalize(iosysid)))
                return ret;

        } /* next rearranger */
    } /* endif my_rank < TARGET_NTASKS */

    /* Finalize the MPI library. */
    printf("%d %s Finalizing...\n", my_rank, TEST_NAME);
    if ((ret = pio_test_finalize(&test_comm)))
        return ret;

    printf("%d %s SUCCESS!!\n", my_rank, TEST_NAME);
    return 0;
}
