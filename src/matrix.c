static void
context_callback_matrix_free(void* m) {
  pgGrB_Matrix *mat = (pgGrB_Matrix *) m;
  GrB_Matrix_free(&mat->A);
}

Datum
matrix_agg_acc(PG_FUNCTION_ARGS)
{
  pgGrB_Matrix_AggState *mstate;
  MemoryContext aggcxt;
  MemoryContext oldcxt;
  Datum *row, *col, *val;

  if (!AggCheckCallContext(fcinfo, &aggcxt))
    elog(ERROR, "aggregate function called in non-aggregate context");

  if (PG_ARGISNULL(1) || PG_ARGISNULL(2) || PG_ARGISNULL(3))
    elog(ERROR, "matrices cannot contain null values");

  oldcxt = MemoryContextSwitchTo(aggcxt);

  /* lazy create a new state */
  if (PG_ARGISNULL(0)) {
    mstate = palloc0(sizeof(pgGrB_Matrix_AggState));
  }  else  {
    mstate = (pgGrB_Matrix_AggState *)PG_GETARG_POINTER(0);
  }

  row = palloc(sizeof(int64));
  col = palloc(sizeof(int64));
  val = palloc(sizeof(int64));

  *row = PG_GETARG_INT64(1);
  *col = PG_GETARG_INT64(2);
  *val = PG_GETARG_INT64(3);

  mstate->rows = lappend(mstate->rows, row);
  mstate->cols = lappend(mstate->cols, col);
  mstate->vals = lappend(mstate->vals, val);

  MemoryContextSwitchTo(oldcxt);

  PG_RETURN_POINTER(mstate);
}

Datum
matrix_final_int4(PG_FUNCTION_ARGS) {
  GrB_Info info;
  pgGrB_Matrix *retval;

  MemoryContextCallback *ctxcb;

  pgGrB_Matrix_AggState *mstate = (pgGrB_Matrix_AggState*)PG_GETARG_POINTER(0);
  size_t n = 0, count = list_length(mstate->rows);
  GrB_Index *row_indices, *col_indices;
  int64 *matrix_vals;

  ListCell *li, *lj, *lv;

  if (PG_ARGISNULL(0))
    PG_RETURN_NULL();

  mstate = (pgGrB_Matrix_AggState *)PG_GETARG_POINTER(0);

  retval = (pgGrB_Matrix*)palloc(sizeof(pgGrB_Matrix));

  ctxcb = (MemoryContextCallback*) palloc(sizeof(MemoryContextCallback));
  ctxcb->func = context_callback_matrix_free;
  ctxcb->arg = retval;
  MemoryContextRegisterResetCallback(CurTransactionContext, ctxcb);

  row_indices = (GrB_Index*) palloc0(sizeof(GrB_Index) * count);
  col_indices = (GrB_Index*) palloc0(sizeof(GrB_Index) * count);
  matrix_vals = (int64*) palloc0(sizeof(int64) * count);

  forthree (li, (mstate)->rows, lj, (mstate)->cols, lv, (mstate)->vals) {
    /* elog(NOTICE, "%lu", DatumGetInt64(*(Datum*)lfirst(li))); */
    /* elog(NOTICE, "%lu", DatumGetInt64(*(Datum*)lfirst(lj))); */
    /* elog(NOTICE, "%f", DatumGetFloat4(*(Datum*)lfirst(lv))); */
    row_indices[n] = DatumGetInt64(*(Datum*)lfirst(li));
    col_indices[n] = DatumGetInt64(*(Datum*)lfirst(lj));
    matrix_vals[n] = DatumGetInt64(*(Datum*)lfirst(lv));
    n++;
  }

  CHECK(GrB_Matrix_new(&(retval->A),
                       GrB_INT64,
                       count,
                       count));

  CHECK(GrB_Matrix_build(retval->A,
                         row_indices,
                         col_indices,
                         matrix_vals,
                         count,
                         GrB_SECOND_FP32));
  PG_RETURN_POINTER(retval);
}


Datum
matrix_extract(PG_FUNCTION_ARGS) {
  GrB_Info info;
  FuncCallContext  *funcctx;
  TupleDesc tupdesc;
  Datum result;

  Datum values[3];
  bool nulls[3] = {false, false, false};
  HeapTuple tuple;
  GrB_Index nvals = 0;
  pgGrB_Matrix *mat;
  pgGrB_Matrix_ExtractState *state;

  if (SRF_IS_FIRSTCALL()) {
    MemoryContext oldcontext;

    funcctx = SRF_FIRSTCALL_INIT();
    oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);
    mat = (pgGrB_Matrix *) PG_GETARG_POINTER(0);

    state = (pgGrB_Matrix_ExtractState*)palloc(sizeof(pgGrB_Matrix_ExtractState));
    CHECK(GrB_Matrix_nvals(&nvals, mat->A));

    state->rows = (GrB_Index*) palloc0(sizeof(GrB_Index) * nvals);
    state->cols = (GrB_Index*) palloc0(sizeof(GrB_Index) * nvals);
    state->vals = (int64*) palloc0(sizeof(int64) * nvals);

    CHECK(GrB_Matrix_extractTuples(state->rows,
                                   state->cols,
                                   state->vals,
                                   &nvals,
                                   mat->A));
    state->mat = mat;
    funcctx->max_calls = nvals;
    funcctx->user_fctx = (void*)state;

    /* One-time setup code appears here: */
    if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
      ereport(ERROR,
              (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
               errmsg("function returning record called in context "
                      "that cannot accept type record")));
    BlessTupleDesc(tupdesc);
    funcctx->tuple_desc = tupdesc;

    MemoryContextSwitchTo(oldcontext);
  }

  funcctx = SRF_PERCALL_SETUP();
  state = (pgGrB_Matrix_ExtractState*)funcctx->user_fctx;
  mat = state->mat;

  if (funcctx->call_cntr < funcctx->max_calls) {
    values[0] = Int64GetDatum(state->rows[funcctx->call_cntr]);
    values[1] = Int64GetDatum(state->cols[funcctx->call_cntr]);
    values[2] = Int64GetDatum(state->vals[funcctx->call_cntr]);

    tuple = heap_form_tuple(funcctx->tuple_desc, values, nulls);
    result = HeapTupleGetDatum(tuple);
    SRF_RETURN_NEXT(funcctx, result);
  } else {
    SRF_RETURN_DONE(funcctx);
  }
}

Datum
matrix_in(PG_FUNCTION_ARGS)
{
  GrB_Info info;
  pgGrB_Matrix *retval;

  MemoryContextCallback *ctxcb;

  Datum arr;
  ArrayType *vals;
  FunctionCallInfoData locfcinfo;
  int ndim, *dims, *lb;
  int count;

  GrB_Index *row_indices, *col_indices;
  int64 *matrix_vals, *data;

  /* A comment from the pg source...
   *
   * Normally one would call array_recv() using DirectFunctionCall3, but
   * that does not work since array_recv wants to cache some data using
   * fcinfo->flinfo->fn_extra.  So we need to pass it our own flinfo
   * parameter.
   */

  /* arr = DirectFunctionCall3(array_in, */
  /*                           PG_GETARG_DATUM(0), */
  /*                           ObjectIdGetDatum(INT2OID), */
  /*                           Int32GetDatum(-1)); */

  InitFunctionCallInfoData(locfcinfo,
                           fcinfo->flinfo,
                           3,
                           InvalidOid,
                           NULL,
                           NULL);

  locfcinfo.arg[0] = PG_GETARG_DATUM(0);
  locfcinfo.arg[1] = ObjectIdGetDatum(INT8OID);
  locfcinfo.arg[2] = Int32GetDatum(-1);
  locfcinfo.argnull[0] = false;
  locfcinfo.argnull[1] = false;
  locfcinfo.argnull[2] = false;

  arr = array_in(&locfcinfo);

  Assert(!locfcinfo.isnull);

  vals = DatumGetArrayTypeP(arr);

  if (ARR_HASNULL(vals)) {
    ereport(ERROR, (errmsg("Array may not contain NULLs")));
  }

  ndim = ARR_NDIM(vals);
  if (ndim != 2) {
    ereport(ERROR, (errmsg("Two-dimesional arrays are required")));
  }

  dims = ARR_DIMS(vals);

  if (dims[0] != 3) {
    ereport(ERROR, (errmsg("First dimension must contain 3 arrays")));
  }

  lb = ARR_LBOUND(vals);
  count = dims[0] + lb[0] - 1;

  row_indices = (GrB_Index*) palloc0(sizeof(GrB_Index) * count);
  col_indices = (GrB_Index*) palloc0(sizeof(GrB_Index) * count);
  matrix_vals = (int64*) palloc0(sizeof(int64) * count);

  data = (int64*)ARR_DATA_PTR(vals);

  for (int i = 0; i < count; i++) {
    row_indices[i] = data[i];
    col_indices[i] = data[i+count];
    matrix_vals[i] = data[i+count+count];
  }

  retval = (pgGrB_Matrix*) palloc(sizeof(pgGrB_Matrix));

  ctxcb = (MemoryContextCallback*) palloc(sizeof(MemoryContextCallback));
  ctxcb->func = context_callback_matrix_free;
  ctxcb->arg = retval;
  MemoryContextRegisterResetCallback(CurrentMemoryContext, ctxcb);

  CHECK(GrB_Matrix_new(&(retval->A), GrB_INT64, count + 1, count + 1));

  CHECK(GrB_Matrix_build(retval->A,
                         row_indices,
                         col_indices,
                         matrix_vals,
                         count,
                         GrB_PLUS_FP32));

  PG_RETURN_POINTER(retval);
}

Datum
matrix_out(PG_FUNCTION_ARGS)
{
  GrB_Info info;
  pgGrB_Matrix *mat = (pgGrB_Matrix *) PG_GETARG_POINTER(0);
  char *result;
  GrB_Index nrows, ncols, nvals;

  GrB_Index *row_indices, *col_indices;
  int64 *matrix_vals;

  CHECK(GrB_Matrix_nrows(&nrows, mat->A));
  CHECK(GrB_Matrix_ncols(&ncols, mat->A));
  CHECK(GrB_Matrix_nvals(&nvals, mat->A));

  row_indices = (GrB_Index*) palloc0(sizeof(GrB_Index) * nvals);
  col_indices = (GrB_Index*) palloc0(sizeof(GrB_Index) * nvals);
  matrix_vals = (int64*) palloc0(sizeof(int64) * nvals);

  CHECK(GrB_Matrix_extractTuples(row_indices,
                                 col_indices,
                                 matrix_vals,
                                 &nvals,
                                 mat->A));

  result = psprintf("{%lu, %lu, %lu}::matrix", nrows, ncols, nvals);
  PG_RETURN_CSTRING(result);
}

Datum
matrix_nrows(PG_FUNCTION_ARGS) {
  GrB_Info info;
  pgGrB_Matrix *mat;
  GrB_Index count;
  mat = (pgGrB_Matrix *) PG_GETARG_POINTER(0);
  CHECK(GrB_Matrix_nrows(&count, mat->A));
  return Int64GetDatum(count);
}

Datum
matrix_ncols(PG_FUNCTION_ARGS) {
  GrB_Info info;
  pgGrB_Matrix *mat;
  GrB_Index count;
  mat = (pgGrB_Matrix *) PG_GETARG_POINTER(0);
  CHECK(GrB_Matrix_ncols(&count, mat->A));
  return Int64GetDatum(count);
}

Datum
matrix_nvals(PG_FUNCTION_ARGS) {
  GrB_Info info;
  pgGrB_Matrix *mat;
  GrB_Index count;
  mat = (pgGrB_Matrix *) PG_GETARG_POINTER(0);
  CHECK(GrB_Matrix_nvals(&count, mat->A));
  return Int64GetDatum(count);
}

Datum
matrix_x_matrix(PG_FUNCTION_ARGS) {
  GrB_Info info;
  pgGrB_Matrix *A, *B, *C;
  GrB_Index m, n;

  A = (pgGrB_Matrix *) PG_GETARG_POINTER(0);
  CHECK(GrB_Matrix_nrows(&m, A->A));

  B = (pgGrB_Matrix *) PG_GETARG_POINTER(1);
  CHECK(GrB_Matrix_ncols(&n, B->A));

  C = (pgGrB_Matrix *) palloc0(sizeof(pgGrB_Matrix));

  CHECK(GrB_Matrix_new (&(C->A), GrB_INT64, m, n));

  CHECK(GrB_mxm(C->A, NULL, NULL, GxB_PLUS_TIMES_INT64, A->A, B->A, NULL));
  PG_RETURN_POINTER(C);
}

Datum
matrix_ewise_mult(PG_FUNCTION_ARGS) {
  GrB_Info info;
  pgGrB_Matrix *A, *B, *C;
  GrB_Index m, n;

  A = (pgGrB_Matrix *) PG_GETARG_POINTER(0);
  CHECK(GrB_Matrix_nrows(&m, A->A));

  B = (pgGrB_Matrix *) PG_GETARG_POINTER(1);
  CHECK(GrB_Matrix_ncols(&n, B->A));

  C = (pgGrB_Matrix *) palloc0(sizeof(pgGrB_Matrix));

  CHECK(GrB_Matrix_new (&(C->A), GrB_INT64, m, n));

  CHECK(GrB_eWiseMult(C->A, NULL, NULL, GrB_TIMES_INT64, A->A, B->A, NULL));
  PG_RETURN_POINTER(C);
}