/**
 * @file   tiledb.cc
 *
 * @section LICENSE
 *
 * The MIT License
 *
 * @copyright Copyright (c) 2017 TileDB, Inc.
 * @copyright Copyright (c) 2016 MIT and Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * @section DESCRIPTION
 *
 * This file defines the C API of TileDB.
 */

#include "aio_request.h"
#include "array_schema.h"
#include "basic_array.h"
#include "storage_manager.h"

/* ****************************** */
/*            VERSION             */
/* ****************************** */

void tiledb_version(int* major, int* minor, int* rev) {
  *major = TILEDB_VERSION_MAJOR;
  *minor = TILEDB_VERSION_MINOR;
  *rev = TILEDB_VERSION_REVISION;
}

/* ********************************* */
/*              CONFIG               */
/* ********************************* */

struct tiledb_config_t {
  // The configurator instance
  tiledb::Configurator* config_;
};

tiledb_config_t* tiledb_config_create() {
  // Create struct
  tiledb_config_t* config = (tiledb_config_t*)malloc(sizeof(tiledb_config_t));
  if (config == nullptr)
    return nullptr;

  // Create configurator
  config->config_ = new tiledb::Configurator();
  if (config->config_ == nullptr) {  // Allocation error
    free(config);
    return nullptr;
  }

  // Return the new object
  return config;
}

int tiledb_config_free(tiledb_config_t* config) {
  // Trivial case
  if (config == nullptr)
    return TILEDB_OK;

  // Free configurator
  if (config->config_ != nullptr)
    delete config->config_;

  // Free struct
  free(config);

  // Success
  return TILEDB_OK;
}

#ifdef HAVE_MPI
int tiledb_config_set_mpi_comm(tiledb_config_t* config, MPI_Comm* mpi_comm) {
  // Sanity check
  if (config == nullptr || config->config_ == nullptr)
    return TILEDB_ERR;

  // Set MPI communicator
  config->config_->set_mpi_comm(mpi_comm);

  // Success
  return TILEDB_OK;
}
#endif

int tiledb_config_set_read_method(
    tiledb_config_t* config, tiledb_io_t read_method) {
  // Sanity check
  if (config == nullptr || config->config_ == nullptr)
    return TILEDB_ERR;

  // Set read method
  config->config_->set_read_method(static_cast<tiledb::IOMethod>(read_method));

  // Success
  return TILEDB_OK;
}

int tiledb_config_set_write_method(
    tiledb_config_t* config, tiledb_io_t write_method) {
  // Sanity check
  if (config == nullptr || config->config_ == nullptr)
    return TILEDB_ERR;

  // Set write method
  config->config_->set_write_method(
      static_cast<tiledb::IOMethod>(write_method));

  // Success
  return TILEDB_OK;
}

/* ****************************** */
/*            CONTEXT             */
/* ****************************** */

struct tiledb_ctx_t {
  // storage manager instance
  tiledb::StorageManager* storage_manager_;
  // last error associated with this context
  tiledb::Status* last_error_;
};

static bool save_error(tiledb_ctx_t* ctx, const tiledb::Status& st) {
  // No error
  if (st.ok()) {
    return false;
  }

  // Delete previous error
  if (ctx->last_error_ != nullptr)
    delete ctx->last_error_;

  // Store new error and return
  ctx->last_error_ = new tiledb::Status(st);
  return true;
}

tiledb::Status sanity_check(tiledb_ctx_t* ctx) {
  if (ctx == nullptr || ctx->storage_manager_ == nullptr) {
    return tiledb::Status::Error("Invalid TileDB context");
  }
  return tiledb::Status::Ok();
}

int tiledb_ctx_init(tiledb_ctx_t** ctx, const tiledb_config_t* tiledb_config) {
  // Initialize context
  tiledb::Status st;
  *ctx = (tiledb_ctx_t*)malloc(sizeof(struct tiledb_ctx_t));
  if (*ctx == nullptr) {
    return TILEDB_OOM;
  }

  // Create storage manager and initialize with the config object
  (*ctx)->storage_manager_ = new tiledb::StorageManager();
  (*ctx)->last_error_ = nullptr;
  tiledb::Configurator* config =
      (tiledb_config == nullptr) ? nullptr : tiledb_config->config_;
  if (save_error(*ctx, (*ctx)->storage_manager_->init(config)))
    return TILEDB_ERR;

  // Success
  return TILEDB_OK;
}

int tiledb_ctx_finalize(tiledb_ctx_t* ctx) {
  if (ctx == nullptr)
    return TILEDB_OK;

  tiledb::Status st;
  if (ctx->storage_manager_ != nullptr) {
    st = ctx->storage_manager_->finalize();
    delete ctx->storage_manager_;
  }

  if (ctx->last_error_ != nullptr)
    delete ctx->last_error_;

  free(ctx);

  return st.ok() ? TILEDB_OK : TILEDB_ERR;
}

typedef struct tiledb_error_t {
  // pointer to a copy of the last TileDB error associated with a given ctx
  const tiledb::Status* status_;
} tiledb_error_t;

tiledb_error_t* tiledb_error_last(tiledb_ctx_t* ctx) {
  if (ctx == nullptr || ctx->last_error_ == nullptr)
    return nullptr;
  tiledb_error_t* err = (tiledb_error_t*)malloc(sizeof(tiledb_error_t));
  err->status_ = new tiledb::Status(*ctx->last_error_);
  return err;
}

const char* tiledb_error_message(tiledb_error_t* err) {
  if (err == nullptr || err->status_->ok()) {
    return "";
  }
  return err->status_->to_string().c_str();
}

int tiledb_error_free(tiledb_error_t* err) {
  if (err != nullptr) {
    if (err->status_ != nullptr) {
      delete err->status_;
    }
    free(err);
  }
  return TILEDB_OK;
}

/* ****************************** */
/*              GROUP             */
/* ****************************** */

int tiledb_group_create(tiledb_ctx_t* ctx, const char* group) {
  if (!sanity_check(ctx).ok())
    return TILEDB_ERR;

  // Create the group
  if (save_error(ctx, ctx->storage_manager_->group_create(group)))
    return TILEDB_ERR;

  // Success
  return TILEDB_OK;
}

/* ********************************* */
/*            BASIC ARRAY            */
/* ********************************* */

typedef struct tiledb_basic_array_t {
  tiledb::BasicArray* basic_array_;
  tiledb_ctx_t* ctx_;
} tiledb_basic_array_t;

int tiledb_basic_array_create(tiledb_ctx_t* ctx, const char* name) {
  // Sanity check
  if (!sanity_check(ctx).ok())
    return TILEDB_ERR;

  // Create the basic array
  if (save_error(ctx, ctx->storage_manager_->basic_array_create(name)))
    return TILEDB_ERR;

  // Success
  return TILEDB_OK;
}

/* ****************************** */
/*              ARRAY             */
/* ****************************** */

typedef struct tiledb_array_t {
  tiledb::Array* array_;
  tiledb_ctx_t* ctx_;
} tiledb_array_t;

tiledb::Status sanity_check(const tiledb_array_t* tiledb_array) {
  if (tiledb_array == nullptr || tiledb_array->array_ == nullptr ||
      tiledb_array->ctx_ == nullptr) {
    return tiledb::Status::Error("Invalid TileDB array");
  }
  return tiledb::Status::Ok();
}

tiledb::Status sanity_check(tiledb_array_schema_t* sch) {
  if (sch == nullptr)
    return tiledb::Status::Error("Invalid TileDB array schema");
  return tiledb::Status::Ok();
}

int tiledb_array_set_schema(
    tiledb_ctx_t* ctx,
    tiledb_array_schema_t* tiledb_array_schema,
    const char* array_name,
    const char** attributes,
    int attribute_num,
    int64_t capacity,
    tiledb_layout_t cell_order,
    const int* cell_val_num,
    const tiledb_compressor_t* compression,
    int dense,
    const char** dimensions,
    int dim_num,
    const void* domain,
    size_t domain_len,
    const void* tile_extents,
    size_t tile_extents_len,
    tiledb_layout_t tile_order,
    const tiledb_datatype_t* types) {
  if (!sanity_check(ctx).ok())
    return TILEDB_ERR;

  if (save_error(ctx, sanity_check(tiledb_array_schema)))
    return TILEDB_ERR;

  size_t array_name_len = strlen(array_name);
  tiledb_array_schema->array_name_ = (char*)malloc(array_name_len + 1);
  if (tiledb_array_schema->array_name_ == nullptr)
    return TILEDB_OOM;
  strcpy(tiledb_array_schema->array_name_, array_name);

  // Set attributes and number of attributes
  tiledb_array_schema->attribute_num_ = attribute_num;
  tiledb_array_schema->attributes_ =
      (char**)malloc(attribute_num * sizeof(char*));
  if (tiledb_array_schema->attributes_ == nullptr)
    return TILEDB_OOM;
  for (int i = 0; i < attribute_num; ++i) {
    size_t attribute_len = strlen(attributes[i]);
    tiledb_array_schema->attributes_[i] = (char*)malloc(attribute_len + 1);
    if (tiledb_array_schema->attributes_[i] == nullptr)
      return TILEDB_OOM;
    strcpy(tiledb_array_schema->attributes_[i], attributes[i]);
  }

  // Set dimensions
  tiledb_array_schema->dim_num_ = dim_num;
  tiledb_array_schema->dimensions_ = (char**)malloc(dim_num * sizeof(char*));
  if (tiledb_array_schema->dimensions_ == nullptr)
    return TILEDB_OOM;
  for (int i = 0; i < dim_num; ++i) {
    size_t dimension_len = strlen(dimensions[i]);
    tiledb_array_schema->dimensions_[i] = (char*)malloc(dimension_len + 1);
    if (tiledb_array_schema->dimensions_[i] == nullptr)
      return TILEDB_OOM;
    strcpy(tiledb_array_schema->dimensions_[i], dimensions[i]);
  }

  // Set dense
  tiledb_array_schema->dense_ = dense;

  // Set domain
  tiledb_array_schema->domain_ = malloc(domain_len);
  memcpy(tiledb_array_schema->domain_, domain, domain_len);

  // Set tile extents
  if (tile_extents == nullptr) {
    tiledb_array_schema->tile_extents_ = nullptr;
  } else {
    tiledb_array_schema->tile_extents_ = malloc(tile_extents_len);
    if (tiledb_array_schema->tile_extents_ == nullptr)
      return TILEDB_OOM;
    memcpy(tiledb_array_schema->tile_extents_, tile_extents, tile_extents_len);
  }

  // Set types
  tiledb_array_schema->types_ =
      (tiledb_datatype_t*)malloc((attribute_num + 1) * sizeof(int));
  if (tiledb_array_schema->types_ == nullptr)
    return TILEDB_OOM;
  for (int i = 0; i < attribute_num + 1; ++i)
    tiledb_array_schema->types_[i] = types[i];

  // Set cell val num
  if (cell_val_num == nullptr) {
    tiledb_array_schema->cell_val_num_ = nullptr;
  } else {
    tiledb_array_schema->cell_val_num_ =
        (int*)malloc((attribute_num) * sizeof(int));
    if (tiledb_array_schema->cell_val_num_ == nullptr)
      return TILEDB_OOM;
    for (int i = 0; i < attribute_num; ++i) {
      tiledb_array_schema->cell_val_num_[i] = cell_val_num[i];
    }
  }

  // Set cell and tile order
  tiledb_array_schema->cell_order_ = cell_order;
  tiledb_array_schema->tile_order_ = tile_order;

  // Set capacity
  tiledb_array_schema->capacity_ = capacity;

  // Set compression
  if (compression == nullptr) {
    tiledb_array_schema->compressor_ = nullptr;
  } else {
    tiledb_array_schema->compressor_ =
        (tiledb_compressor_t*)malloc((attribute_num + 1) * sizeof(int));
    if (tiledb_array_schema->compressor_ == nullptr)
      return TILEDB_OOM;
    for (int i = 0; i < attribute_num + 1; ++i)
      tiledb_array_schema->compressor_[i] = compression[i];
  }

  return TILEDB_OK;
}

int tiledb_array_create(
    tiledb_ctx_t* ctx, const tiledb_array_schema_t* array_schema) {
  if (!sanity_check(ctx).ok())
    return TILEDB_ERR;

  // Copy array schema to a C struct
  tiledb::ArraySchemaC array_schema_c;
  array_schema_c.array_name_ = array_schema->array_name_;
  array_schema_c.attributes_ = array_schema->attributes_;
  array_schema_c.attribute_num_ = array_schema->attribute_num_;
  array_schema_c.capacity_ = array_schema->capacity_;
  array_schema_c.cell_order_ = array_schema->cell_order_;
  array_schema_c.cell_val_num_ = array_schema->cell_val_num_;
  array_schema_c.compressor_ = array_schema->compressor_;
  array_schema_c.dense_ = array_schema->dense_;
  array_schema_c.dimensions_ = array_schema->dimensions_;
  array_schema_c.dim_num_ = array_schema->dim_num_;
  array_schema_c.domain_ = array_schema->domain_;
  array_schema_c.tile_extents_ = array_schema->tile_extents_;
  array_schema_c.tile_order_ = array_schema->tile_order_;
  array_schema_c.types_ = array_schema->types_;

  // Create the array
  if (save_error(ctx, ctx->storage_manager_->array_create(&array_schema_c)))
    return TILEDB_ERR;

  return TILEDB_OK;
}

int tiledb_array_init(
    tiledb_ctx_t* ctx,
    tiledb_array_t** tiledb_array,
    const char* array,
    tiledb_array_mode_t mode,
    const void* subarray,
    const char** attributes,
    int attribute_num) {
  if (!sanity_check(ctx).ok())
    return TILEDB_ERR;

  // Allocate memory for the array struct
  *tiledb_array = (tiledb_array_t*)malloc(sizeof(struct tiledb_array_t));
  if (tiledb_array == nullptr)
    return TILEDB_OOM;

  // Set TileDB context
  (*tiledb_array)->ctx_ = ctx;

  // Init the array
  if (save_error(
          ctx,
          ctx->storage_manager_->array_init(
              (*tiledb_array)->array_,
              array,
              static_cast<tiledb::ArrayMode>(mode),
              subarray,
              attributes,
              attribute_num))) {
    free(*tiledb_array);
    return TILEDB_ERR;
  };

  return TILEDB_OK;
}

int tiledb_array_reset_subarray(
    const tiledb_array_t* tiledb_array, const void* subarray) {
  if (!sanity_check(tiledb_array).ok())
    return TILEDB_ERR;

  tiledb_ctx_t* ctx = tiledb_array->ctx_;

  if (save_error(ctx, tiledb_array->array_->reset_subarray(subarray)))
    return TILEDB_ERR;

  return TILEDB_OK;
}

int tiledb_array_reset_attributes(
    const tiledb_array_t* tiledb_array,
    const char** attributes,
    int attribute_num) {
  if (!sanity_check(tiledb_array).ok())
    return TILEDB_ERR;

  tiledb_ctx_t* ctx = tiledb_array->ctx_;

  // Re-Init the array
  if (save_error(
          ctx,
          tiledb_array->array_->reset_attributes(attributes, attribute_num)))
    return TILEDB_ERR;

  return TILEDB_OK;
}

int tiledb_array_get_schema(
    const tiledb_array_t* tiledb_array,
    tiledb_array_schema_t* tiledb_array_schema) {
  if (!sanity_check(tiledb_array).ok())
    return TILEDB_ERR;

  // Get the array schema
  tiledb::ArraySchemaC array_schema_c;
  tiledb_array->array_->array_schema()->array_schema_export(&array_schema_c);

  // Copy the array schema C struct to the output
  tiledb_array_schema->array_name_ = array_schema_c.array_name_;
  tiledb_array_schema->attributes_ = array_schema_c.attributes_;
  tiledb_array_schema->attribute_num_ = array_schema_c.attribute_num_;
  tiledb_array_schema->capacity_ = array_schema_c.capacity_;
  tiledb_array_schema->cell_order_ = array_schema_c.cell_order_;
  tiledb_array_schema->cell_val_num_ = array_schema_c.cell_val_num_;
  tiledb_array_schema->compressor_ = array_schema_c.compressor_;
  tiledb_array_schema->dense_ = array_schema_c.dense_;
  tiledb_array_schema->dimensions_ = array_schema_c.dimensions_;
  tiledb_array_schema->dim_num_ = array_schema_c.dim_num_;
  tiledb_array_schema->domain_ = array_schema_c.domain_;
  tiledb_array_schema->tile_extents_ = array_schema_c.tile_extents_;
  tiledb_array_schema->tile_order_ = array_schema_c.tile_order_;
  tiledb_array_schema->types_ = array_schema_c.types_;

  return TILEDB_OK;
}

int tiledb_array_load_schema(
    tiledb_ctx_t* ctx,
    const char* array,
    tiledb_array_schema_t* tiledb_array_schema) {
  if (!sanity_check(ctx).ok())
    return TILEDB_ERR;

  // Get the array schema
  tiledb::ArraySchema* array_schema = new tiledb::ArraySchema();
  if (save_error(ctx, array_schema->load(array))) {
    delete array_schema;
    return TILEDB_ERR;
  }

  // Export array schema
  tiledb::ArraySchemaC array_schema_c;
  array_schema->array_schema_export(&array_schema_c);

  // Copy the array schema C struct to the output
  tiledb_array_schema->array_name_ = array_schema_c.array_name_;
  tiledb_array_schema->attributes_ = array_schema_c.attributes_;
  tiledb_array_schema->attribute_num_ = array_schema_c.attribute_num_;
  tiledb_array_schema->capacity_ = array_schema_c.capacity_;
  tiledb_array_schema->cell_order_ = array_schema_c.cell_order_;
  tiledb_array_schema->cell_val_num_ = array_schema_c.cell_val_num_;
  tiledb_array_schema->compressor_ = array_schema_c.compressor_;
  tiledb_array_schema->dense_ = array_schema_c.dense_;
  tiledb_array_schema->dimensions_ = array_schema_c.dimensions_;
  tiledb_array_schema->dim_num_ = array_schema_c.dim_num_;
  tiledb_array_schema->domain_ = array_schema_c.domain_;
  tiledb_array_schema->tile_extents_ = array_schema_c.tile_extents_;
  tiledb_array_schema->tile_order_ = array_schema_c.tile_order_;
  tiledb_array_schema->types_ = array_schema_c.types_;

  // Clean up
  delete array_schema;

  // Success
  return TILEDB_OK;
}

int tiledb_array_free_schema(tiledb_array_schema_t* tiledb_array_schema) {
  // Trivial case
  if (tiledb_array_schema == nullptr)
    return TILEDB_OK;

  // Free array name
  if (tiledb_array_schema->array_name_ != nullptr)
    free(tiledb_array_schema->array_name_);

  // Free attributes
  if (tiledb_array_schema->attributes_ != nullptr) {
    for (int i = 0; i < tiledb_array_schema->attribute_num_; ++i)
      if (tiledb_array_schema->attributes_[i] != nullptr)
        free(tiledb_array_schema->attributes_[i]);
    free(tiledb_array_schema->attributes_);
  }

  // Free dimensions
  if (tiledb_array_schema->dimensions_ != nullptr) {
    for (int i = 0; i < tiledb_array_schema->dim_num_; ++i)
      if (tiledb_array_schema->dimensions_[i] != nullptr)
        free(tiledb_array_schema->dimensions_[i]);
    free(tiledb_array_schema->dimensions_);
  }

  // Free domain
  if (tiledb_array_schema->domain_ != nullptr)
    free(tiledb_array_schema->domain_);

  // Free tile extents
  if (tiledb_array_schema->tile_extents_ != nullptr)
    free(tiledb_array_schema->tile_extents_);

  // Free types
  if (tiledb_array_schema->types_ != nullptr)
    free(tiledb_array_schema->types_);

  // Free compression
  if (tiledb_array_schema->compressor_ != nullptr)
    free(tiledb_array_schema->compressor_);

  // Free cell val num
  if (tiledb_array_schema->cell_val_num_ != nullptr)
    free(tiledb_array_schema->cell_val_num_);

  return TILEDB_OK;
}

int tiledb_array_write(
    const tiledb_array_t* tiledb_array,
    const void** buffers,
    const size_t* buffer_sizes) {
  if (!sanity_check(tiledb_array).ok())
    return TILEDB_ERR;

  tiledb_ctx_t* ctx = tiledb_array->ctx_;

  if (save_error(ctx, sanity_check(tiledb_array)))
    return TILEDB_ERR;

  if (save_error(ctx, tiledb_array->array_->write(buffers, buffer_sizes)))
    return TILEDB_ERR;

  return TILEDB_OK;
}

int tiledb_array_read(
    const tiledb_array_t* tiledb_array, void** buffers, size_t* buffer_sizes) {
  if (!sanity_check(tiledb_array).ok())
    return TILEDB_ERR;

  tiledb_ctx_t* ctx = tiledb_array->ctx_;

  if (save_error(ctx, sanity_check(tiledb_array)))
    return TILEDB_ERR;

  if (save_error(ctx, tiledb_array->array_->read(buffers, buffer_sizes)))
    return TILEDB_ERR;

  return TILEDB_OK;
}

int tiledb_array_overflow(
    const tiledb_array_t* tiledb_array, int attribute_id) {
  if (!sanity_check(tiledb_array).ok())
    return TILEDB_ERR;

  tiledb_ctx_t* ctx = tiledb_array->ctx_;

  if (save_error(ctx, sanity_check(tiledb_array)))
    return TILEDB_ERR;

  return (int)tiledb_array->array_->overflow(attribute_id);
}

int tiledb_array_consolidate(tiledb_ctx_t* ctx, const char* array) {
  if (!sanity_check(ctx).ok())
    return TILEDB_ERR;

  if (save_error(ctx, ctx->storage_manager_->array_consolidate(array)))
    return TILEDB_ERR;

  return TILEDB_OK;
}

int tiledb_array_finalize(tiledb_array_t* tiledb_array) {
  if (!sanity_check(tiledb_array).ok())
    return TILEDB_ERR;

  tiledb_ctx_t* ctx = tiledb_array->ctx_;

  if (save_error(ctx, sanity_check(tiledb_array)))
    return TILEDB_ERR;

  if (save_error(
          ctx,
          tiledb_array->ctx_->storage_manager_->array_finalize(
              tiledb_array->array_))) {
    free(tiledb_array);
    return TILEDB_ERR;
  }

  free(tiledb_array);

  return TILEDB_OK;
}

int tiledb_array_sync(tiledb_array_t* tiledb_array) {
  if (!sanity_check(tiledb_array).ok())
    return TILEDB_ERR;

  tiledb_ctx_t* ctx = tiledb_array->ctx_;

  if (save_error(ctx, sanity_check(tiledb_array)))
    return TILEDB_ERR;

  if (save_error(
          ctx,
          tiledb_array->ctx_->storage_manager_->array_sync(
              tiledb_array->array_)))
    return TILEDB_ERR;

  return TILEDB_OK;
}

int tiledb_array_sync_attribute(
    tiledb_array_t* tiledb_array, const char* attribute) {
  if (!sanity_check(tiledb_array).ok())
    return TILEDB_ERR;

  tiledb_ctx_t* ctx = tiledb_array->ctx_;

  if (save_error(ctx, sanity_check(tiledb_array)))
    return TILEDB_ERR;

  if (save_error(
          ctx,
          tiledb_array->ctx_->storage_manager_->array_sync_attribute(
              tiledb_array->array_, attribute)))
    return TILEDB_ERR;

  return TILEDB_OK;
}

typedef struct tiledb_array_iterator_t {
  tiledb::ArrayIterator* array_it_;
  tiledb_ctx_t* ctx_;
} tiledb_array_iterator_t;

tiledb::Status sanity_check(const tiledb_array_iterator_t* tiledb_array_it) {
  if (tiledb_array_it == nullptr || tiledb_array_it->array_it_ == nullptr ||
      tiledb_array_it->ctx_ == nullptr) {
    return tiledb::Status::Error("Invalid TileDB array iterator");
  }
  return tiledb::Status::Ok();
}

int tiledb_array_iterator_init(
    tiledb_ctx_t* ctx,
    tiledb_array_iterator_t** tiledb_array_it,
    const char* array,
    tiledb_array_mode_t mode,
    const void* subarray,
    const char** attributes,
    int attribute_num,
    void** buffers,
    size_t* buffer_sizes) {
  if (!sanity_check(ctx).ok())
    return TILEDB_ERR;

  *tiledb_array_it =
      (tiledb_array_iterator_t*)malloc(sizeof(struct tiledb_array_iterator_t));
  if (*tiledb_array_it == nullptr)
    return TILEDB_OOM;

  (*tiledb_array_it)->ctx_ = ctx;

  if (save_error(
          ctx,
          ctx->storage_manager_->array_iterator_init(
              (*tiledb_array_it)->array_it_,
              array,
              static_cast<tiledb::ArrayMode>(mode),
              subarray,
              attributes,
              attribute_num,
              buffers,
              buffer_sizes))) {
    free(*tiledb_array_it);
    return TILEDB_ERR;
  }

  return TILEDB_OK;
}

int tiledb_array_iterator_get_value(
    tiledb_array_iterator_t* tiledb_array_it,
    int attribute_id,
    const void** value,
    size_t* value_size) {
  if (!sanity_check(tiledb_array_it).ok())
    return TILEDB_ERR;

  tiledb_ctx_t* ctx = tiledb_array_it->ctx_;

  if (save_error(ctx, sanity_check(tiledb_array_it)))
    return TILEDB_ERR;

  // Get value
  if (save_error(
          ctx,
          tiledb_array_it->array_it_->get_value(
              attribute_id, value, value_size)))
    return TILEDB_ERR;

  return TILEDB_OK;
}

int tiledb_array_iterator_next(tiledb_array_iterator_t* tiledb_array_it) {
  if (!sanity_check(tiledb_array_it).ok())
    return TILEDB_ERR;

  tiledb_ctx_t* ctx = tiledb_array_it->ctx_;

  if (save_error(ctx, sanity_check(tiledb_array_it)))
    return TILEDB_ERR;

  if (save_error(ctx, tiledb_array_it->array_it_->next()))
    return TILEDB_ERR;

  return TILEDB_OK;
}

int tiledb_array_iterator_end(tiledb_array_iterator_t* tiledb_array_it) {
  if (!sanity_check(tiledb_array_it).ok())
    return TILEDB_ERR;

  tiledb_ctx_t* ctx = tiledb_array_it->ctx_;

  if (save_error(ctx, sanity_check(tiledb_array_it)))
    return TILEDB_ERR;

  return (int)tiledb_array_it->array_it_->end();
}

int tiledb_array_iterator_finalize(tiledb_array_iterator_t* tiledb_array_it) {
  if (!sanity_check(tiledb_array_it).ok())
    return TILEDB_ERR;

  tiledb_ctx_t* ctx = tiledb_array_it->ctx_;

  if (save_error(ctx, sanity_check(tiledb_array_it)))
    return TILEDB_ERR;

  if (save_error(
          ctx,
          tiledb_array_it->ctx_->storage_manager_->array_iterator_finalize(
              tiledb_array_it->array_it_))) {
    free(tiledb_array_it);
    return TILEDB_ERR;
  }

  free(tiledb_array_it);

  return TILEDB_OK;
}

/* ****************************** */
/*            METADATA            */
/* ****************************** */

typedef struct tiledb_metadata_t {
  tiledb::Metadata* metadata_;
  tiledb_ctx_t* ctx_;
} tiledb_metadata_t;

tiledb::Status sanity_check(const tiledb_metadata_t* tiledb_metadata) {
  if (tiledb_metadata == nullptr || tiledb_metadata->metadata_ == nullptr ||
      tiledb_metadata->ctx_ == nullptr)
    return tiledb::Status::Error("Invalid TileDB metadata");
  return tiledb::Status::Ok();
}

tiledb::Status sanity_check(const tiledb_metadata_schema_t* sch) {
  if (sch == nullptr)
    return tiledb::Status::Error("Invalid metadata schema");
  return tiledb::Status::Ok();
}

int tiledb_metadata_set_schema(
    tiledb_ctx_t* ctx,
    tiledb_metadata_schema_t* tiledb_metadata_schema,
    const char* metadata_name,
    const char** attributes,
    int attribute_num,
    int64_t capacity,
    const int* cell_val_num,
    const tiledb_compressor_t* compression,
    const tiledb_datatype_t* types) {
  if (!sanity_check(ctx).ok())
    return TILEDB_ERR;

  if (save_error(ctx, sanity_check(tiledb_metadata_schema)))
    return TILEDB_ERR;

  size_t metadata_name_len = strlen(metadata_name);
  tiledb_metadata_schema->metadata_name_ = (char*)malloc(metadata_name_len + 1);
  if (tiledb_metadata_schema->metadata_name_ == nullptr)
    return TILEDB_OOM;
  strcpy(tiledb_metadata_schema->metadata_name_, metadata_name);

  /* Set attributes and number of attributes. */
  tiledb_metadata_schema->attribute_num_ = attribute_num;
  tiledb_metadata_schema->attributes_ =
      (char**)malloc(attribute_num * sizeof(char*));
  if (tiledb_metadata_schema->attributes_ == nullptr)
    return TILEDB_OOM;
  for (int i = 0; i < attribute_num; ++i) {
    size_t attribute_len = strlen(attributes[i]);
    tiledb_metadata_schema->attributes_[i] = (char*)malloc(attribute_len + 1);
    if (tiledb_metadata_schema->attributes_[i] == nullptr)
      return TILEDB_OOM;
    strcpy(tiledb_metadata_schema->attributes_[i], attributes[i]);
  }

  // Set types
  tiledb_metadata_schema->types_ =
      (tiledb_datatype_t*)malloc((attribute_num + 1) * sizeof(int));
  if (tiledb_metadata_schema->types_ == nullptr)
    return TILEDB_OOM;
  for (int i = 0; i < attribute_num + 1; ++i)
    tiledb_metadata_schema->types_[i] = types[i];

  // Set cell val num
  if (cell_val_num == nullptr) {
    tiledb_metadata_schema->cell_val_num_ = nullptr;
  } else {
    tiledb_metadata_schema->cell_val_num_ =
        (int*)malloc((attribute_num) * sizeof(int));
    if (tiledb_metadata_schema->cell_val_num_ == nullptr)
      return TILEDB_OOM;
    for (int i = 0; i < attribute_num; ++i) {
      tiledb_metadata_schema->cell_val_num_[i] = cell_val_num[i];
    }
  }

  // Set capacity
  tiledb_metadata_schema->capacity_ = capacity;

  // Set compression
  if (compression == nullptr) {
    tiledb_metadata_schema->compressor_ = nullptr;
  } else {
    tiledb_metadata_schema->compressor_ =
        (tiledb_compressor_t*)malloc((attribute_num + 1) * sizeof(int));
    if (tiledb_metadata_schema->compressor_ == nullptr)
      return TILEDB_OOM;
    for (int i = 0; i < attribute_num + 1; ++i)
      tiledb_metadata_schema->compressor_[i] = compression[i];
  }
  return TILEDB_OK;
}

int tiledb_metadata_create(
    tiledb_ctx_t* ctx, const tiledb_metadata_schema_t* metadata_schema) {
  if (!sanity_check(ctx).ok())
    return TILEDB_ERR;

  if (save_error(ctx, sanity_check(metadata_schema)))
    return TILEDB_ERR;

  // Copy metadata schema to the proper struct
  tiledb::MetadataSchemaC metadata_schema_c;
  metadata_schema_c.metadata_name_ = metadata_schema->metadata_name_;
  metadata_schema_c.attributes_ = metadata_schema->attributes_;
  metadata_schema_c.attribute_num_ = metadata_schema->attribute_num_;
  metadata_schema_c.capacity_ = metadata_schema->capacity_;
  metadata_schema_c.cell_val_num_ = metadata_schema->cell_val_num_;
  metadata_schema_c.compressor_ = metadata_schema->compressor_;
  metadata_schema_c.types_ = metadata_schema->types_;

  if (save_error(
          ctx, ctx->storage_manager_->metadata_create(&metadata_schema_c)))
    return TILEDB_ERR;

  return TILEDB_OK;
}

int tiledb_metadata_init(
    tiledb_ctx_t* ctx,
    tiledb_metadata_t** tiledb_metadata,
    const char* metadata,
    tiledb_metadata_mode_t mode,
    const char** attributes,
    int attribute_num) {
  if (!sanity_check(ctx).ok())
    return TILEDB_ERR;

  // Allocate memory for the array struct
  *tiledb_metadata =
      (tiledb_metadata_t*)malloc(sizeof(struct tiledb_metadata_t));
  if (*tiledb_metadata == nullptr)
    return TILEDB_OOM;

  // Set TileDB context
  (*tiledb_metadata)->ctx_ = ctx;

  // Init the metadata
  if (save_error(
          ctx,
          ctx->storage_manager_->metadata_init(
              (*tiledb_metadata)->metadata_,
              metadata,
              mode,
              attributes,
              attribute_num))) {
    free(*tiledb_metadata);
    return TILEDB_ERR;
  }

  return TILEDB_OK;
}

int tiledb_metadata_reset_attributes(
    const tiledb_metadata_t* tiledb_metadata,
    const char** attributes,
    int attribute_num) {
  if (!sanity_check(tiledb_metadata).ok())
    return TILEDB_ERR;

  tiledb_ctx_t* ctx = tiledb_metadata->ctx_;

  // Reset attributes
  if (save_error(
          ctx,
          tiledb_metadata->metadata_->reset_attributes(
              attributes, attribute_num)))
    return TILEDB_ERR;

  return TILEDB_OK;
}

int tiledb_metadata_get_schema(
    const tiledb_metadata_t* tiledb_metadata,
    tiledb_metadata_schema_t* tiledb_metadata_schema) {
  if (!sanity_check(tiledb_metadata).ok())
    return TILEDB_ERR;

  tiledb_ctx_t* ctx = tiledb_metadata->ctx_;

  if (save_error(ctx, sanity_check(tiledb_metadata)))
    return TILEDB_ERR;

  // Get the metadata schema
  tiledb::MetadataSchemaC metadata_schema_c;
  tiledb_metadata->metadata_->array_schema()->array_schema_export(
      &metadata_schema_c);

  // Copy the metadata schema C struct to the output
  tiledb_metadata_schema->metadata_name_ = metadata_schema_c.metadata_name_;
  tiledb_metadata_schema->attributes_ = metadata_schema_c.attributes_;
  tiledb_metadata_schema->attribute_num_ = metadata_schema_c.attribute_num_;
  tiledb_metadata_schema->capacity_ = metadata_schema_c.capacity_;
  tiledb_metadata_schema->cell_val_num_ = metadata_schema_c.cell_val_num_;
  tiledb_metadata_schema->compressor_ = metadata_schema_c.compressor_;
  tiledb_metadata_schema->types_ = metadata_schema_c.types_;

  return TILEDB_OK;
}

int tiledb_metadata_load_schema(
    tiledb_ctx_t* ctx,
    const char* metadata,
    tiledb_metadata_schema_t* tiledb_metadata_schema) {
  if (!sanity_check(ctx).ok())
    return TILEDB_ERR;

  // Get the array schema
  tiledb::ArraySchema* array_schema;

  if (save_error(
          ctx,
          ctx->storage_manager_->metadata_load_schema(metadata, array_schema)))
    return TILEDB_ERR;

  tiledb::MetadataSchemaC metadata_schema_c;
  array_schema->array_schema_export(&metadata_schema_c);

  // Copy the metadata schema C struct to the output
  tiledb_metadata_schema->metadata_name_ = metadata_schema_c.metadata_name_;
  tiledb_metadata_schema->attributes_ = metadata_schema_c.attributes_;
  tiledb_metadata_schema->attribute_num_ = metadata_schema_c.attribute_num_;
  tiledb_metadata_schema->capacity_ = metadata_schema_c.capacity_;
  tiledb_metadata_schema->cell_val_num_ = metadata_schema_c.cell_val_num_;
  tiledb_metadata_schema->compressor_ = metadata_schema_c.compressor_;
  tiledb_metadata_schema->types_ = metadata_schema_c.types_;

  delete array_schema;

  return TILEDB_OK;
}

int tiledb_metadata_free_schema(
    tiledb_metadata_schema_t* tiledb_metadata_schema) {
  if (tiledb_metadata_schema == nullptr)
    return TILEDB_OK;

  // Free name
  if (tiledb_metadata_schema->metadata_name_ != nullptr)
    free(tiledb_metadata_schema->metadata_name_);

  // Free attributes
  if (tiledb_metadata_schema->attributes_ != nullptr) {
    for (int i = 0; i < tiledb_metadata_schema->attribute_num_; ++i)
      if (tiledb_metadata_schema->attributes_[i] != nullptr)
        free(tiledb_metadata_schema->attributes_[i]);
    free(tiledb_metadata_schema->attributes_);
  }

  // Free types
  if (tiledb_metadata_schema->types_ != nullptr)
    free(tiledb_metadata_schema->types_);

  // Free compression
  if (tiledb_metadata_schema->compressor_ != nullptr)
    free(tiledb_metadata_schema->compressor_);

  // Free cell val num
  if (tiledb_metadata_schema->cell_val_num_ != nullptr)
    free(tiledb_metadata_schema->cell_val_num_);

  return TILEDB_OK;
}

int tiledb_metadata_write(
    const tiledb_metadata_t* tiledb_metadata,
    const char* keys,
    size_t keys_size,
    const void** buffers,
    const size_t* buffer_sizes) {
  if (!sanity_check(tiledb_metadata).ok())
    return TILEDB_ERR;

  tiledb_ctx_t* ctx = tiledb_metadata->ctx_;

  if (save_error(
          ctx,
          tiledb_metadata->metadata_->write(
              keys, keys_size, buffers, buffer_sizes)))
    return TILEDB_ERR;

  return TILEDB_OK;
}

int tiledb_metadata_read(
    const tiledb_metadata_t* tiledb_metadata,
    const char* key,
    void** buffers,
    size_t* buffer_sizes) {
  if (!sanity_check(tiledb_metadata).ok())
    return TILEDB_ERR;

  tiledb_ctx_t* ctx = tiledb_metadata->ctx_;

  if (save_error(
          ctx, tiledb_metadata->metadata_->read(key, buffers, buffer_sizes)))
    return TILEDB_ERR;

  return TILEDB_OK;
}

int tiledb_metadata_overflow(
    const tiledb_metadata_t* tiledb_metadata, int attribute_id) {
  if (!sanity_check(tiledb_metadata).ok())
    return TILEDB_ERR;

  return (int)tiledb_metadata->metadata_->overflow(attribute_id);
}

int tiledb_metadata_consolidate(tiledb_ctx_t* ctx, const char* metadata) {
  if (!sanity_check(ctx).ok())
    return TILEDB_ERR;

  if (save_error(ctx, ctx->storage_manager_->metadata_consolidate(metadata)))
    return TILEDB_ERR;

  return TILEDB_OK;
}

int tiledb_metadata_finalize(tiledb_metadata_t* tiledb_metadata) {
  if (!sanity_check(tiledb_metadata).ok())
    return TILEDB_ERR;

  tiledb_ctx_t* ctx = tiledb_metadata->ctx_;

  if (save_error(
          ctx,
          tiledb_metadata->ctx_->storage_manager_->metadata_finalize(
              tiledb_metadata->metadata_))) {
    free(tiledb_metadata);
    return TILEDB_ERR;
  }
  return TILEDB_OK;
}

typedef struct tiledb_metadata_iterator_t {
  tiledb::MetadataIterator* metadata_it_;
  tiledb_ctx_t* ctx_;
} tiledb_metadata_iterator_t;

tiledb::Status sanity_check(
    const tiledb_metadata_iterator_t* tiledb_metadata_it) {
  if (tiledb_metadata_it == nullptr ||
      tiledb_metadata_it->metadata_it_ == nullptr ||
      tiledb_metadata_it->ctx_ == nullptr) {
    return tiledb::Status::Error("Invalid TileDB metadata iterator");
  }
  return tiledb::Status::Ok();
}

int tiledb_metadata_iterator_init(
    tiledb_ctx_t* ctx,
    tiledb_metadata_iterator_t** tiledb_metadata_it,
    const char* metadata,
    const char** attributes,
    int attribute_num,
    void** buffers,
    size_t* buffer_sizes) {
  if (!sanity_check(ctx).ok())
    return TILEDB_ERR;

  // Allocate memory for the metadata struct
  *tiledb_metadata_it = (tiledb_metadata_iterator_t*)malloc(
      sizeof(struct tiledb_metadata_iterator_t));
  if (*tiledb_metadata_it == nullptr)
    return TILEDB_ERR;

  // Set TileDB context
  (*tiledb_metadata_it)->ctx_ = ctx;

  // Initialize the metadata iterator
  if (save_error(
          ctx,
          ctx->storage_manager_->metadata_iterator_init(
              (*tiledb_metadata_it)->metadata_it_,
              metadata,
              attributes,
              attribute_num,
              buffers,
              buffer_sizes))) {
    free(*tiledb_metadata_it);
    return TILEDB_ERR;
  }
  return TILEDB_OK;
}

int tiledb_metadata_iterator_get_value(
    tiledb_metadata_iterator_t* tiledb_metadata_it,
    int attribute_id,
    const void** value,
    size_t* value_size) {
  if (!sanity_check(tiledb_metadata_it).ok())
    return TILEDB_ERR;

  tiledb_ctx_t* ctx = tiledb_metadata_it->ctx_;

  if (save_error(ctx, sanity_check(tiledb_metadata_it)))
    return TILEDB_ERR;

  if (save_error(
          ctx,
          tiledb_metadata_it->metadata_it_->get_value(
              attribute_id, value, value_size)))
    return TILEDB_ERR;

  return TILEDB_OK;
}

int tiledb_metadata_iterator_next(
    tiledb_metadata_iterator_t* tiledb_metadata_it) {
  if (!sanity_check(tiledb_metadata_it).ok())
    return TILEDB_ERR;

  tiledb_ctx_t* ctx = tiledb_metadata_it->ctx_;

  // Advance metadata iterator
  if (save_error(ctx, tiledb_metadata_it->metadata_it_->next()))
    return TILEDB_ERR;

  return TILEDB_OK;
}

int tiledb_metadata_iterator_end(
    tiledb_metadata_iterator_t* tiledb_metadata_it) {
  if (!sanity_check(tiledb_metadata_it).ok())
    return TILEDB_ERR;

  // Check if the metadata iterator reached its end
  return (int)tiledb_metadata_it->metadata_it_->end();
}

int tiledb_metadata_iterator_finalize(
    tiledb_metadata_iterator_t* tiledb_metadata_it) {
  if (!sanity_check(tiledb_metadata_it).ok())
    return TILEDB_ERR;

  tiledb_ctx_t* ctx = tiledb_metadata_it->ctx_;

  // Finalize metadata iterator
  if (save_error(
          ctx,
          tiledb_metadata_it->ctx_->storage_manager_
              ->metadata_iterator_finalize(tiledb_metadata_it->metadata_it_))) {
    free(tiledb_metadata_it);
    return TILEDB_ERR;
  }

  free(tiledb_metadata_it);

  return TILEDB_OK;
}

/* ****************************** */
/*       DIRECTORY MANAGEMENT     */
/* ****************************** */

int tiledb_dir_type(tiledb_ctx_t* ctx, const char* dir) {
  if (ctx == nullptr)
    return TILEDB_ERR;
  return ctx->storage_manager_->dir_type(dir);
}

int tiledb_clear(tiledb_ctx_t* ctx, const char* dir) {
  if (!sanity_check(ctx).ok())
    return TILEDB_ERR;

  // TODO: do this everywhere
  if (dir == nullptr) {
    save_error(
        ctx, tiledb::Status::Error("Invalid directory argument is NULL"));
    return TILEDB_ERR;
  }

  if (save_error(ctx, ctx->storage_manager_->clear(dir)))
    return TILEDB_ERR;

  return TILEDB_OK;
}

int tiledb_delete(tiledb_ctx_t* ctx, const char* dir) {
  if (!sanity_check(ctx).ok())
    return TILEDB_ERR;

  if (save_error(ctx, ctx->storage_manager_->delete_entire(dir)))
    return TILEDB_ERR;

  return TILEDB_OK;
}

int tiledb_move(tiledb_ctx_t* ctx, const char* old_dir, const char* new_dir) {
  if (!sanity_check(ctx).ok())
    return TILEDB_ERR;

  if (save_error(ctx, ctx->storage_manager_->move(old_dir, new_dir)))
    return TILEDB_ERR;

  return TILEDB_OK;
}

int tiledb_ls(
    tiledb_ctx_t* ctx,
    const char* parent_dir,
    char** dirs,
    tiledb_object_t* dir_types,
    int* dir_num) {
  if (!sanity_check(ctx).ok())
    return TILEDB_ERR;

  if (save_error(
          ctx,
          ctx->storage_manager_->ls(parent_dir, dirs, dir_types, *dir_num)))
    return TILEDB_ERR;

  return TILEDB_OK;
}

int tiledb_ls_c(tiledb_ctx_t* ctx, const char* parent_dir, int* dir_num) {
  if (!sanity_check(ctx).ok())
    return TILEDB_ERR;

  if (save_error(ctx, ctx->storage_manager_->ls_c(parent_dir, *dir_num)))
    return TILEDB_ERR;

  return TILEDB_OK;
}

/* ****************************** */
/*     ASYNCHRONOUS I/O (AIO)     */
/* ****************************** */

int tiledb_array_aio_read(
    const tiledb_array_t* tiledb_array,
    TileDB_AIO_Request* tiledb_aio_request) {
  if (!sanity_check(tiledb_array).ok())
    return TILEDB_ERR;

  tiledb_ctx_t* ctx = tiledb_array->ctx_;

  // Copy the AIO request
  tiledb::AIO_Request* aio_request =
      (tiledb::AIO_Request*)malloc(sizeof(struct tiledb::AIO_Request));
  aio_request->id_ = (size_t)tiledb_aio_request;
  aio_request->buffers_ = tiledb_aio_request->buffers_;
  aio_request->buffer_sizes_ = tiledb_aio_request->buffer_sizes_;
  aio_request->mode_ =
      static_cast<tiledb_array_mode_t>(tiledb_array->array_->mode());
  aio_request->status_ = &(tiledb_aio_request->status_);
  aio_request->subarray_ = tiledb_aio_request->subarray_;
  aio_request->completion_handle_ = tiledb_aio_request->completion_handle_;
  aio_request->completion_data_ = tiledb_aio_request->completion_data_;

  // Submit the AIO read request
  if (save_error(ctx, tiledb_array->array_->aio_read(aio_request)))
    return TILEDB_ERR;

  return TILEDB_OK;
}

int tiledb_array_aio_write(
    const tiledb_array_t* tiledb_array,
    TileDB_AIO_Request* tiledb_aio_request) {
  if (!sanity_check(tiledb_array).ok())
    return TILEDB_ERR;

  tiledb_ctx_t* ctx = tiledb_array->ctx_;

  // Copy the AIO request
  tiledb::AIO_Request* aio_request =
      (tiledb::AIO_Request*)malloc(sizeof(struct tiledb::AIO_Request));
  aio_request->id_ = (size_t)tiledb_aio_request;
  aio_request->buffers_ = tiledb_aio_request->buffers_;
  aio_request->buffer_sizes_ = tiledb_aio_request->buffer_sizes_;
  aio_request->mode_ =
      static_cast<tiledb_array_mode_t>(tiledb_array->array_->mode());
  aio_request->status_ = &(tiledb_aio_request->status_);
  aio_request->subarray_ = tiledb_aio_request->subarray_;
  aio_request->completion_handle_ = tiledb_aio_request->completion_handle_;
  aio_request->completion_data_ = tiledb_aio_request->completion_data_;

  // Submit the AIO write request
  if (save_error(ctx, tiledb_array->array_->aio_write(aio_request)))
    return TILEDB_ERR;

  return TILEDB_OK;
}
