//===----------------------------------------------------------------------===//
//
//                         Peloton
//
// type_system.h
//
// Identification: src/include/codegen/type/type_system.h
//
// Copyright (c) 2015-2017, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <vector>

#include "common/macros.h"
#include "type/types.h"

namespace peloton {
namespace codegen {

class CodeGen;
class Value;

namespace type {

class Type;

//===----------------------------------------------------------------------===//
// This class contains all the type-system functionality for the codegen
// component. In reality, there is tremendous overlap with the Peloton type
// system. In the future, this class should replace that one ...
//
// This class implements an operator table similar in spirit to Postgres. In
// Peloton, operators are categorized as casting, comparison, unary or binary
// operators. Every SQL type configures a TypeSystem with supported operators.
// As in Postgres, each operator can be configured to be overriden by a user-
// provided implementation.
//===----------------------------------------------------------------------===//
class TypeSystem {
 public:
  //===--------------------------------------------------------------------===//
  //
  // Casting operation
  //
  //===--------------------------------------------------------------------===//
  class Cast {
   public:
    // Virtual destructor
    virtual ~Cast() = default;

    // Does this cast support casting from the given type to the given type?
    virtual bool SupportsTypes(const Type &from_type,
                               const Type &to_type) const = 0;

    // Perform the cast on the given value to the provided type
    virtual Value DoCast(CodeGen &codegen, const Value &value,
                         const Type &to_type) const = 0;
  };

  //===--------------------------------------------------------------------===//
  //
  // SimpleNullableCast
  //
  // An abstract base class for cast operations. This class performs generic
  // NULL checking logic common across **most** casting operations. If the input
  // is NULLable, an if-then-else construct is generated to perform the NULL
  // check. Subclasses implement casting logic assuming non-NULLable inputs. If
  // the input is **not** NULLable, the  NULL check is elided completely.
  //
  //===--------------------------------------------------------------------===//
  class SimpleNullableCast : public TypeSystem::Cast {
   public:
    Value DoCast(CodeGen &codegen, const Value &value,
                 const Type &to_type) const override;

   protected:
    // Perform the cast assuming the input is not NULLable
    virtual Value CastImpl(CodeGen &codegen, const Value &value,
                           const Type &to_type) const = 0;
  };

  struct CastInfo {
    peloton::type::TypeId from_type;
    peloton::type::TypeId to_type;
    const Cast &cast_operation;
  };

  //===--------------------------------------------------------------------===//
  //
  // The generic comparison interface for all comparisons between all types
  //
  //===--------------------------------------------------------------------===//
  class Comparison {
   public:
    // Virtual destructor
    virtual ~Comparison() = default;

    // Does this instance support comparison of the values of the given left and
    // right SQL types?
    virtual bool SupportsTypes(const Type &left_type,
                               const Type &right_type) const = 0;

    // Main comparison operators
    virtual Value DoCompareLt(CodeGen &codegen, const Value &left,
                              const Value &right) const = 0;

    virtual Value DoCompareLte(CodeGen &codegen, const Value &left,
                               const Value &right) const = 0;

    virtual Value DoCompareEq(CodeGen &codegen, const Value &left,
                              const Value &right) const = 0;

    virtual Value DoCompareNe(CodeGen &codegen, const Value &left,
                              const Value &right) const = 0;

    virtual Value DoCompareGt(CodeGen &codegen, const Value &left,
                              const Value &right) const = 0;

    virtual Value DoCompareGte(CodeGen &codegen, const Value &left,
                               const Value &right) const = 0;

    // Perform a comparison used for sorting. We need a stable and transitive
    // sorting comparison operator here. The operator returns:
    //  < 0 - if the left value comes before the right value when sorted
    //  = 0 - if the left value is equivalent to the right element
    //  > 0 - if the left value comes after the right value when sorted
    virtual Value DoCompareForSort(CodeGen &codegen, const Value &left,
                                   const Value &right) const = 0;
  };

  //===--------------------------------------------------------------------===//
  //
  // SimpleNullableComparison
  //
  // An abstract base class for comparison operations. This class handles
  // generic NULL-checking code, this, allowing subclass comparisons to assume
  // and work on non-null inputs. Depending on the NULLness of its inputs,
  // this class properly computes (or entirely elides) the NULL bit, deferring
  // to the subclass to implement the comparison.
  //
  //===--------------------------------------------------------------------===//
  class SimpleNullableComparison : public Comparison {
   public:
    Value DoCompareLt(CodeGen &codegen, const Value &left,
                      const Value &right) const override;
    Value DoCompareLte(CodeGen &codegen, const Value &left,
                       const Value &right) const override;
    Value DoCompareEq(CodeGen &codegen, const Value &left,
                      const Value &right) const override;
    Value DoCompareNe(CodeGen &codegen, const Value &left,
                      const Value &right) const override;
    Value DoCompareGt(CodeGen &codegen, const Value &left,
                      const Value &right) const override;
    Value DoCompareGte(CodeGen &codegen, const Value &left,
                       const Value &right) const override;
    Value DoCompareForSort(CodeGen &codegen, const Value &left,
                           const Value &right) const override;

   protected:
    // The non-null comparison implementations
    virtual Value CompareLtImpl(CodeGen &codegen, const Value &left,
                                const Value &right) const = 0;
    virtual Value CompareLteImpl(CodeGen &codegen, const Value &left,
                                 const Value &right) const = 0;
    virtual Value CompareEqImpl(CodeGen &codegen, const Value &left,
                                const Value &right) const = 0;
    virtual Value CompareNeImpl(CodeGen &codegen, const Value &left,
                                const Value &right) const = 0;
    virtual Value CompareGtImpl(CodeGen &codegen, const Value &left,
                                const Value &right) const = 0;
    virtual Value CompareGteImpl(CodeGen &codegen, const Value &left,
                                 const Value &right) const = 0;
    virtual Value CompareForSortImpl(CodeGen &codegen, const Value &left,
                                     const Value &right) const = 0;
  };

  struct ComparisonInfo {
    // The operation
    const Comparison &comparison;
  };

  //===--------------------------------------------------------------------===//
  // A unary operator (i.e., an operator that accepts a single argument)
  //===--------------------------------------------------------------------===//
  class UnaryOperator {
   public:
    virtual ~UnaryOperator() {}

    // Does this unary operator support values of the given type?
    virtual bool SupportsType(const Type &type) const = 0;

    // What is the SQL type of the result of applying the unary operator on a
    // value of the provided type?
    virtual Type ResultType(const Type &val_type) const = 0;

    // Apply the operator on the given value
    virtual Value DoWork(CodeGen &codegen, const Value &val) const = 0;
  };

  class UnaryOperatorWithNullPropagation : public UnaryOperator {
   public:
    UnaryOperatorWithNullPropagation(const UnaryOperator &inner_op)
        : inner_op_(inner_op) {}

    // Does this unary operator support values of the given type?
    bool SupportsType(const Type &type) const override;

    // What is the SQL type of the result of applying the unary operator on a
    // value of the provided type?
    Type ResultType(const Type &val_type) const override;

    // Apply the operator on the given value
    Value DoWork(CodeGen &codegen, const Value &val) const override;

   private:
    const UnaryOperator &inner_op_;
  };

  struct UnaryOpInfo {
    // The ID of the operation
    OperatorId op_id;

    // The operation
    const UnaryOperator &unary_operation;
  };

  //===--------------------------------------------------------------------===//
  // A binary operator (i.e., an operator that accepts two arguments)
  //===--------------------------------------------------------------------===//
  class BinaryOperator {
   public:
    virtual ~BinaryOperator() {}

    // Does this binary operator support the two provided input types?
    virtual bool SupportsTypes(const Type &left_type,
                               const Type &right_type) const = 0;

    // What is the SQL type of the result of applying the binary operator on the
    // provided left and right value types?
    virtual Type ResultType(const Type &left_type,
                            const Type &right_type) const = 0;

    // Execute the actual operator
    virtual Value DoWork(CodeGen &codegen, const Value &left,
                         const Value &right, OnError on_error) const = 0;
  };

  class BinaryOperatorWithNullPropagation : public BinaryOperator {
   public:
    BinaryOperatorWithNullPropagation(
        const TypeSystem::BinaryOperator &inner_op)
        : inner_op_(inner_op) {}

    bool SupportsTypes(const Type &left_type,
                       const Type &right_type) const override;

    Type ResultType(const Type &left_type,
                    const Type &right_type) const override;

    Value DoWork(CodeGen &codegen, const Value &left, const Value &right,
                 OnError on_error) const override;

   private:
    const BinaryOperator &inner_op_;
  };

  struct BinaryOpInfo {
    // The ID of the operation
    OperatorId op_id;

    // The operation
    const BinaryOperator &binary_operation;
  };

  // An n-ary function
  class NaryOperator {
   public:
    virtual ~NaryOperator() {}

    // Does this operator support the provided input argument types?
    virtual bool SupportsTypes(const std::vector<Type> &arg_types) const = 0;

    // What is type of the result produced by this operator?
    virtual Type ResultType(const std::vector<Type> &arg_types) const = 0;

    // Execute the actual operator
    virtual Value DoWork(CodeGen &codegen, const std::vector<Value> &input_args,
                         OnError on_error) const = 0;
  };

  struct NaryOpInfo {
    // The ID of the operation
    OperatorId op_id;

    // The operation
    const NaryOperator &nary_operation;
  };

 public:
  TypeSystem(const std::vector<peloton::type::TypeId> &implicit_cast_table,
             const std::vector<CastInfo> &explicit_cast_table,
             const std::vector<ComparisonInfo> &comparison_table,
             const std::vector<UnaryOpInfo> &unary_op_table,
             const std::vector<BinaryOpInfo> &binary_op_table,
             const std::vector<NaryOpInfo> &nary_op_table);

  // Can values of the provided type be implicitly casted into a value of the
  // other provided type?
  static bool CanImplicitlyCastTo(const Type &from_type, const Type &to_type);

  // Lookup comparison handler for the given type
  static const Cast *GetCast(const Type &from_type, const Type &to_type);

  // Lookup comparison handler for the given type
  static const Comparison *GetComparison(const Type &left_type,
                                         Type &left_casted_type,
                                         const Type &right_type,
                                         Type &right_casted_type);

  // Lookup the given binary operator that works on the left and right types
  static const UnaryOperator *GetUnaryOperator(OperatorId op_id,
                                               const Type &input_type);

  // Lookup the given binary operator that works on the left and right types
  static const BinaryOperator *GetBinaryOperator(OperatorId op_id,
                                                 const Type &left_type,
                                                 Type &left_casted_type,
                                                 const Type &right_type,
                                                 Type &right_casted_type);

  // Lookup the given nary operator that operators on the provided types
  static const NaryOperator *GetNaryOperator(
      OperatorId op_id, const std::vector<Type> &arg_types);

 private:
  // The list of types a given type can be implicitly casted to
  const std::vector<peloton::type::TypeId> &implicit_cast_table_;

  // The table of explicit casting functions
  const std::vector<CastInfo> &explicit_cast_table_;

  // The comparison table
  const std::vector<ComparisonInfo> &comparison_table_;

  // The table of builtin unary operators
  const std::vector<UnaryOpInfo> &unary_op_table_;

  // The table of builtin binary operators
  const std::vector<BinaryOpInfo> &binary_op_table_;

  // The table of builtin nary operators
  const std::vector<NaryOpInfo> &nary_op_table_;

 private:
  DISALLOW_COPY_AND_MOVE(TypeSystem);
};

}  // namespace type
}  // namespace codegen
}  // namespace peloton