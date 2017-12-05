//===----------------------------------------------------------------------===//
//
//                         Peloton
//
// properties.cpp
//
// Identification: src/optimizer/properties.cpp
//
// Copyright (c) 2015-16, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "optimizer/properties.h"

#include "optimizer/property.h"
#include "optimizer/property_visitor.h"

namespace peloton {
namespace optimizer {

/*************** PropertyColumns *****************/

PropertyColumns::PropertyColumns(
    std::vector<std::shared_ptr<expression::AbstractExpression>> column_exprs)
    : column_exprs_(std::move(column_exprs)) {}

PropertyType PropertyColumns::Type() const { return PropertyType::COLUMNS; }

bool PropertyColumns::operator>=(const Property &r) const {
  // check the type
  if (r.Type() != PropertyType::COLUMNS) {
    return false;
  }
  const PropertyColumns &r_columns =
      *reinterpret_cast<const PropertyColumns *>(&r);
  // lhs is less than rhs if size is smaller
  if (column_exprs_.size() < r_columns.column_exprs_.size()) {
    return false;
  }
  // check that every column in the right hand side property exists in the left
  // hand side property
  for (auto r_column : r_columns.column_exprs_) {
    bool has_column = false;
    for (auto column : column_exprs_) {
      if (column->Equals(r_column.get())) {
        has_column = true;
        break;
      }
    }
    if (has_column == false) return false;
  }

  return true;
}

bool PropertyColumns::HasStarExpression() const {
  for (auto expr : column_exprs_) {
    if (expr->GetExpressionType() == ExpressionType::STAR) return true;
  }
  return false;
}

hash_t PropertyColumns::Hash() const {
  // hash the type
  hash_t hash = Property::Hash();
  for (auto expr : column_exprs_) {
    hash = HashUtil::SumHashes(hash, expr->Hash());
  }
  return hash;
}

void PropertyColumns::Accept(PropertyVisitor *v) const {
  v->Visit((const PropertyColumns *)this);
}

std::string PropertyColumns::ToString() const {
  std::string str = PropertyTypeToString(Type()) + ": ";
  for (auto column_expr : column_exprs_) {
    if (column_expr->GetExpressionType() == ExpressionType::VALUE_TUPLE) {
      str += ((expression::TupleValueExpression *)column_expr.get())
                 ->GetColumnName();
      str += " ";
    } else {
      column_expr->DeduceExpressionName();
      str += column_expr->GetExpressionName();
      str += " ";
    }
  }
  return str + "\n";
}

/*************** PropertyDistinct *****************/

PropertyDistinct::PropertyDistinct(std::vector<
    std::shared_ptr<expression::AbstractExpression>> distinct_column_exprs)
    : distinct_column_exprs_(std::move(distinct_column_exprs)) {
  LOG_TRACE("Size of column property: %ld", distinct_column_exprs_.size());
}

PropertyType PropertyDistinct::Type() const { return PropertyType::DISTINCT; }

bool PropertyDistinct::operator>=(const Property &r) const {
  // check the type
  if (r.Type() != PropertyType::DISTINCT) return false;
  const PropertyDistinct &r_columns =
      *reinterpret_cast<const PropertyDistinct *>(&r);

  // check that every column in the left hand side property exists in the right
  // hand side property. which is the opposite to
  // the condition of propertyColumns
  // e.g. distinct(col_a) >= distinct(col_a, col_b)
  for (auto r_column : r_columns.distinct_column_exprs_) {
    bool has_column = false;
    for (auto column : distinct_column_exprs_) {
      if (column->Equals(r_column.get())) {
        has_column = true;
        break;
      }
    }
    if (has_column == false) return false;
  }

  return true;
}

hash_t PropertyDistinct::Hash() const {
  // hash the type
  hash_t hash = Property::Hash();
  for (auto expr : distinct_column_exprs_) {
    hash = HashUtil::CombineHashes(hash, expr->Hash());
  }
  return hash;
}

void PropertyDistinct::Accept(PropertyVisitor *v) const {
  v->Visit((const PropertyDistinct *)this);
}

std::string PropertyDistinct::ToString() const {
  std::string str = PropertyTypeToString(Type()) + ": ";
  for (auto column_expr : distinct_column_exprs_) {
    if (column_expr->GetExpressionType() == ExpressionType::VALUE_TUPLE) {
      str += ((expression::TupleValueExpression *)column_expr.get())
                 ->GetColumnName();
      str += " ";
    } else {
      // TODO: Add support for other expression
      str += "expr ";
    }
  }
  return str + "\n";
}

/*************** PropertyLimit *****************/

PropertyType PropertyLimit::Type() const { return PropertyType::LIMIT; }

bool PropertyLimit::operator>=(const Property &r) const {
  if (r.Type() != PropertyType::LIMIT) return false;
  const PropertyLimit &r_limit = *reinterpret_cast<const PropertyLimit *>(&r);
  return offset_ == r_limit.offset_ && limit_ == r_limit.limit_;
}

hash_t PropertyLimit::Hash() const {
  hash_t hash = Property::Hash();
  HashUtil::CombineHashes(hash, offset_);
  HashUtil::CombineHashes(hash, limit_);
  return hash;
}

void PropertyLimit::Accept(PropertyVisitor *v) const {
  v->Visit((const PropertyLimit *)this);
}

std::string PropertyLimit::ToString() const {
  std::string res = PropertyTypeToString(Type());
  res += std::to_string(offset_) + " " + std::to_string(limit_) + "\n";
  return res;
}

/*************** PropertySort *****************/

PropertySort::PropertySort(
    std::vector<std::shared_ptr<expression::AbstractExpression>> sort_columns,
    std::vector<bool> sort_ascending)
    : sort_columns_(std::move(sort_columns)),
      sort_ascending_(std::move(sort_ascending)) {}

PropertyType PropertySort::Type() const { return PropertyType::SORT; }

bool PropertySort::operator>=(const Property &r) const {
  // check the type
  if (r.Type() != PropertyType::SORT) return false;
  const PropertySort &r_sort = *reinterpret_cast<const PropertySort *>(&r);

  // All the sorting orders in r must be satisfied
  size_t l_num_sort_columns = sort_columns_.size();
  size_t r_num_sort_columns = r_sort.sort_columns_.size();
  PL_ASSERT(num_sort_columns == r_sort.sort_ascending_.size());
  size_t l_sort_col_idx = 0;
  // We want to ensure that Sort(a, b, c, d, e) >= Sort(a, c, e)
  for (size_t r_sort_col_idx = 0; r_sort_col_idx < r_num_sort_columns;
       ++r_sort_col_idx) {
    while (l_sort_col_idx < l_num_sort_columns &&
           !sort_columns_[l_sort_col_idx]->Equals(
               r_sort.sort_columns_[r_sort_col_idx].get())) {
      ++l_sort_col_idx;
    }
    if (l_sort_col_idx == l_num_sort_columns ||
        sort_ascending_[l_sort_col_idx] !=
            r_sort.sort_ascending_[r_sort_col_idx]) {
      return false;
    }
    ++l_sort_col_idx;
  }
  return true;
}

hash_t PropertySort::Hash() const {
  // hash the type
  hash_t hash = Property::Hash();

  // hash sorting columns
  size_t num_sort_columns = sort_columns_.size();
  for (size_t i = 0; i < num_sort_columns; ++i) {
    hash = HashUtil::CombineHashes(hash, sort_columns_[i]->Hash());
    hash = HashUtil::CombineHashes(hash, sort_ascending_[i]);
  }
  return hash;
}

void PropertySort::Accept(PropertyVisitor *v) const {
  v->Visit((const PropertySort *)this);
}

std::string PropertySort::ToString() const {
  return PropertyTypeToString(Type()) + "\n";
}

}  // namespace optimizer
}  // namespace peloton
